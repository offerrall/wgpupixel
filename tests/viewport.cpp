#include "test.h"
#include <wgpupixel_webgpu.h>
#include <webgpu/wgpu.h>
#include <array>
#include <functional>
#include <limits>
#include <random>
#include <utility>

#ifdef WGPUPIXEL_TEST_FAILURE_INJECTION
namespace {
WGPUCommandBuffer reject_with = nullptr;
bool skip_submission = false;
bool delay_completion = false;
WGPUQueueWorkDoneCallbackInfo delayed_completion = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
}
extern "C" WGPUSubmissionIndex __real_wgpuQueueSubmitForIndex(
    WGPUQueue, std::size_t, const WGPUCommandBuffer*);
extern "C" WGPUSubmissionIndex __wrap_wgpuQueueSubmitForIndex(
    WGPUQueue queue, std::size_t count, const WGPUCommandBuffer* commands) {
    if (reject_with) {
        const auto invalid = std::exchange(reject_with, nullptr);
        // A real, scoped validation failure: this buffer has already been submitted.
        return __real_wgpuQueueSubmitForIndex(queue, 1, &invalid);
    }
    if (std::exchange(skip_submission, false)) return 0;
    return __real_wgpuQueueSubmitForIndex(queue, count, commands);
}
extern "C" WGPUFuture __real_wgpuQueueOnSubmittedWorkDone(
    WGPUQueue, WGPUQueueWorkDoneCallbackInfo);
extern "C" WGPUFuture __wrap_wgpuQueueOnSubmittedWorkDone(
    WGPUQueue queue, WGPUQueueWorkDoneCallbackInfo info) {
    if (std::exchange(delay_completion, false)) {
        delayed_completion = info;
        return {};
    }
    return __real_wgpuQueueOnSubmittedWorkDone(queue, info);
}
#endif

namespace {
using namespace wgpupixel;
using Rgba = std::array<float, 4>; // Linear premultiplied.

Submission reserved_draw(webgpu::Presenter& presenter, const Image& image, WGPUTextureView target,
                         std::uint32_t width, std::uint32_t height,
                         const webgpu::ViewportOptions& options) {
    presenter.reserve(image.size(), options);
    return presenter.draw(image, target, width, height, options);
}

template <class Handle, auto Release> struct Native {
    Handle value{};
    ~Native() {
        if (value) {
            Release(value);
        }
    }
};

struct Target {
    Native<WGPUTexture, wgpuTextureRelease> texture;
    Native<WGPUTextureView, wgpuTextureViewRelease> view;
    std::uint32_t width, height;

    Target(webgpu::NativeContext gpu, std::uint32_t w, std::uint32_t h,
           WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm)
        : width(w), height(h) {
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        desc.size = {w, h, 1};
        desc.format = format;
        texture.value = wgpuDeviceCreateTexture(gpu.device, &desc);
        test::check(texture.value != nullptr, "target texture creation failed");
        view.value = wgpuTextureCreateView(texture.value, nullptr);
        test::check(view.value != nullptr, "target view creation failed");
    }

    std::vector<std::uint8_t> read(webgpu::NativeContext gpu) const {
        const auto stride = (width * 4 + 255) & ~std::uint32_t{255};
        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        desc.size = std::uint64_t(stride) * height;
        Native<WGPUBuffer, wgpuBufferRelease> buffer{wgpuDeviceCreateBuffer(gpu.device, &desc)};
        Native<WGPUCommandEncoder, wgpuCommandEncoderRelease> encoder{
            wgpuDeviceCreateCommandEncoder(gpu.device, nullptr)};
        WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        source.texture = texture.value;
        WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        destination.buffer = buffer.value;
        destination.layout.bytesPerRow = stride;
        destination.layout.rowsPerImage = height;
        const WGPUExtent3D extent{width, height, 1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder.value, &source, &destination, &extent);
        Native<WGPUCommandBuffer, wgpuCommandBufferRelease> commands{
            wgpuCommandEncoderFinish(encoder.value, nullptr)};
        wgpuQueueSubmit(gpu.queue, 1, &commands.value);
        WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Error;
        WGPUBufferMapCallbackInfo callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowProcessEvents;
        callback.userdata1 = &status;
        callback.callback = [](WGPUMapAsyncStatus result, WGPUStringView, void* data, void*) {
            *static_cast<WGPUMapAsyncStatus*>(data) = result;
        };
        wgpuBufferMapAsync(buffer.value, WGPUMapMode_Read, 0, desc.size, callback);
        wgpuDevicePoll(gpu.device, true, nullptr);
        test::check(status == WGPUMapAsyncStatus_Success, "target readback mapping failed");
        const auto* bytes = static_cast<const std::uint8_t*>(
            wgpuBufferGetConstMappedRange(buffer.value, 0, desc.size));
        std::vector<std::uint8_t> pixels(std::size_t(width) * height * 4);
        for (std::uint32_t y = 0; y < height; ++y) {
            std::copy_n(bytes + std::size_t(y) * stride, std::size_t(width) * 4,
                        pixels.data() + std::size_t(y) * width * 4);
        }
        wgpuBufferUnmap(buffer.value);
        return pixels;
    }
};

Rgba over(Rgba top, Rgba bottom) {
    for (std::size_t c = 0; c < 4; ++c) {
        top[c] += bottom[c] * (1 - top[3]);
    }
    return top;
}

Rgba rgba(Color color) {
    return {color.r, color.g, color.b, color.a};
}

std::array<std::uint8_t, 4> encoded(Rgba color) {
    const float alpha = std::clamp(color[3], 0.0f, 1.0f);
    if (alpha <= 0) {
        return {};
    }
    std::array<std::uint8_t, 4> result{};
    for (std::size_t c = 0; c < 3; ++c) {
        // The shader writes premultiplied sRGB for the UI compositor.
        result[c] = static_cast<std::uint8_t>(
            std::floor(test::channel(color[c] / alpha) / 255.0 * alpha * 255 + 0.5));
    }
    result[3] = static_cast<std::uint8_t>(std::floor(alpha * 255 + 0.5f));
    return result;
}

// Expected target bytes from a per-target-pixel model of the view.
std::vector<std::uint8_t> expected(std::uint32_t width, std::uint32_t height,
                                   const std::function<Rgba(std::uint32_t, std::uint32_t)>& model) {
    std::vector<std::uint8_t> result;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto bytes = encoded(model(x, y));
            result.insert(result.end(), bytes.begin(), bytes.end());
        }
    }
    return result;
}

void pixels_near(std::span<const std::uint8_t> actual, std::span<const std::uint8_t> expected,
                 float tolerance = 1) {
    test::check(actual.size() == expected.size(), "pixel byte counts differ");
    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (std::abs(float(actual[i]) - float(expected[i])) > tolerance) {
            test::check(false, "byte " + std::to_string(i) + " (pixel " + std::to_string(i / 4) +
                                   "): expected " + std::to_string(expected[i]) + ", got " +
                                   std::to_string(actual[i]));
        }
    }
}

std::vector<float> random_pixels(std::size_t count, std::uint32_t seed, bool opaque) {
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::vector<float> result;
    for (std::size_t i = 0; i < count; ++i) {
        const float alpha = opaque ? 1.0f : std::array{0.0f, 0.3f, 0.75f, 1.0f}[i % 4];
        for (int c = 0; c < 3; ++c) {
            result.push_back(unit(random) * alpha);
        }
        result.push_back(alpha);
    }
    return result;
}

void upload(Context& ctx, const Image& image, std::span<const float> pixels) {
    auto buffer = ctx.create_upload_buffer(image, {.format = TransferFormat::rgba32_float});
    ctx.write(buffer, {reinterpret_cast<const std::uint8_t*>(pixels.data()), pixels.size_bytes()});
    auto cmd = ctx.create_commands(1);
    cmd.upload(buffer, image);
    ctx.submit_and_wait(cmd);
    ctx.destroy(buffer);
}

void upload(Context& ctx, const Mask& mask, std::span<const std::uint8_t> bytes) {
    auto buffer = ctx.create_upload_buffer(mask);
    ctx.write(buffer, bytes);
    auto cmd = ctx.create_commands(1);
    cmd.upload(buffer, mask);
    ctx.submit_and_wait(cmd);
    ctx.destroy(buffer);
}

Rgba pixel(std::span<const float> pixels, std::uint32_t width, std::uint32_t x, std::uint32_t y) {
    const auto* p = &pixels[(std::size_t(y) * width + x) * 4];
    return {p[0], p[1], p[2], p[3]};
}

Rgba checker(const webgpu::ViewportOptions& options, std::uint32_t x, std::uint32_t y) {
    if (!options.checker_size) {
        return rgba(options.checker_first);
    }
    const auto cell = [&](float target, float origin) {
        return static_cast<long long>(
            std::floor((target + 0.5f - std::round(origin)) / options.checker_size));
    };
    const bool second = (cell(x, options.view.e) + cell(y, options.view.f)) & 1;
    return rgba(second ? options.checker_second : options.checker_first);
}

// Nearest-pixel magnification model for axis-aligned views and optional overlay.
Rgba magnified(std::span<const float> pixels, std::uint32_t width, std::uint32_t height,
               const webgpu::ViewportOptions& options, std::uint32_t x, std::uint32_t y,
               std::span<const std::uint8_t> mask = {}) {
    const auto inverse_view = *inverse(options.view);
    const auto p = inverse_view.map({x + 0.5f, y + 0.5f});
    if (p.x < 0 || p.y < 0 || p.x >= width || p.y >= height) {
        return rgba(options.background);
    }
    const auto px = std::uint32_t(p.x), py = std::uint32_t(p.y);
    auto color = over(pixel(pixels, width, px, py), checker(options, x, y));
    if (!mask.empty()) {
        const float coverage = mask[py * width + px] / 255.0f;
        const float amount = options.overlay_selected ? coverage : 1 - coverage;
        auto tint = rgba(options.overlay_color);
        for (auto& c : tint) {
            c *= amount;
        }
        color = over(tint, color);
    }
    return color;
}

void identity_and_transparency() {
    auto ctx = Context::create();
    auto gpu = webgpu::native_context(ctx);
    constexpr std::uint32_t w = 13, h = 9;
    auto image = ctx.create_image({w, h});
    const auto pixels = random_pixels(w * h, 1, false);
    upload(ctx, image, pixels);
    for (auto format : {WGPUTextureFormat_RGBA8Unorm, WGPUTextureFormat_RGBA8UnormSrgb}) {
        auto presenter = webgpu::Presenter::create(ctx, format);
        Target target(gpu, w, h, format);
        const webgpu::ViewportOptions options{.checker_size = 2};
        ctx.wait(reserved_draw(presenter, image, target.view.value, w, h, options));
        pixels_near(target.read(gpu), expected(w, h, [&](auto x, auto y) {
                        return magnified(pixels, w, h, options, x, y);
                    }));
    }
    ctx.destroy(image);
}

void magnification_and_background() {
    auto ctx = Context::create();
    auto gpu = webgpu::native_context(ctx);
    auto presenter = webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA8Unorm);
    constexpr std::uint32_t w = 7, h = 5;
    auto image = ctx.create_image({w, h});
    const auto pixels = random_pixels(w * h, 2, false);
    upload(ctx, image, pixels);
    Target target(gpu, 40, 30);
    // 300% zoom panned by (10, 5); a non-integer pan keeps squares aligned to the view.
    for (auto view : {Affine::translate(10, 5) * Affine::scale(3),
                      Affine::translate(-2.25f, 3.5f) * Affine::scale(4.5f, 2)}) {
        const webgpu::ViewportOptions options{.view = view,
                                              .background = {0.1f, 0.2f, 0.3f, 1},
                                              .checker_size = 3,
                                              .checker_first = {0.9f, 0.9f, 0.9f, 1},
                                              .checker_second = {0.2f, 0.2f, 0.2f, 1}};
        ctx.wait(reserved_draw(presenter, image, target.view.value, 40, 30, options));
        pixels_near(target.read(gpu), expected(40, 30, [&](auto x, auto y) {
                        return magnified(pixels, w, h, options, x, y);
                    }));
    }
    // Rotate View 90 degrees clockwise: image column x appears as target row x.
    const auto rotated = Affine::translate(h, 0) * Affine::rotate(90);
    Target turned(gpu, h, w);
    ctx.wait(reserved_draw(presenter, image, turned.view.value, h, w,
                           {.view = rotated, .checker_size = 0}));
    pixels_near(turned.read(gpu), expected(h, w, [&](auto x, auto y) {
                    return over(pixel(pixels, w, y, h - 1 - x), {1, 1, 1, 1});
                }));
    ctx.destroy(image);
}

void minification_averages_area() {
    auto ctx = Context::create();
    auto gpu = webgpu::native_context(ctx);
    auto presenter = webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA8Unorm);
    // One-pixel black/white checks alias to black or white with point sampling. Box
    // filtering shows the exact fraction of white pixels in each target pixel's n x n block,
    // counted here: 1/2 for even n, (n^2 +- 1) / 2n^2 for odd n.
    constexpr std::uint32_t size = 60;
    auto image = ctx.create_image({size, size});
    auto cmd = ctx.create_commands(2);
    cmd.checkerboard(image, {.size = 1, .first = {0, 0, 0, 1}, .second = {1, 1, 1, 1}});
    ctx.submit_and_wait(cmd);
    for (std::uint32_t n : {2u, 3u, 4u, 5u, 6u}) {
        const auto extent = size / n;
        Target target(gpu, extent, extent);
        ctx.wait(reserved_draw(presenter, image, target.view.value, extent, extent,
                               {.view = Affine::scale(1.0f / n)}));
        // Pyramid texels cut by odd boxes stand in for their covered part: within a byte.
        pixels_near(target.read(gpu),
                    expected(extent, extent,
                             [&](std::uint32_t x, std::uint32_t y) {
                                 std::uint32_t white = 0;
                                 for (std::uint32_t dy = 0; dy < n; ++dy) {
                                     for (std::uint32_t dx = 0; dx < n; ++dx) {
                                         white += (x * n + dx + y * n + dy) % 2;
                                     }
                                 }
                                 const float v = float(white) / float(n * n);
                                 return Rgba{v, v, v, 1};
                             }),
                    n == 5 ? 4.0f : 1.0f);
    }
    // At 50% each target pixel is exactly the 2 x 2 linear premultiplied box average.
    constexpr std::uint32_t w = 22, h = 14;
    auto random = ctx.create_image({w, h});
    const auto pixels = random_pixels(w * h, 3, false);
    upload(ctx, random, pixels);
    Target half(gpu, w / 2, h / 2);
    const webgpu::ViewportOptions options{.view = Affine::scale(0.5f), .checker_size = 1};
    ctx.wait(reserved_draw(presenter, random, half.view.value, w / 2, h / 2, options));
    pixels_near(half.read(gpu), expected(w / 2, h / 2, [&](auto x, auto y) {
                    Rgba sum{};
                    for (std::uint32_t dy = 0; dy < 2; ++dy) {
                        for (std::uint32_t dx = 0; dx < 2; ++dx) {
                            const auto p = pixel(pixels, w, 2 * x + dx, 2 * y + dy);
                            for (std::size_t c = 0; c < 4; ++c) {
                                sum[c] += p[c] / 4;
                            }
                        }
                    }
                    return over(sum, checker(options, x, y));
                }));
    // The cached pyramid follows image revisions and alternating images.
    Target small(gpu, 12, 12);
    for (const auto& color : {Color{1, 0, 0, 1}, Color{0, 0, 1, 1}}) {
        cmd.fill(image, {.color = color});
        ctx.submit_and_wait(cmd);
        ctx.wait(reserved_draw(presenter, random, half.view.value, w / 2, h / 2, options));
        ctx.wait(reserved_draw(presenter, image, small.view.value, 12, 12,
                               {.view = Affine::scale(0.25f)}));
        pixels_near(small.read(gpu), expected(12, 12, [&](auto, auto) { return rgba(color); }));
    }
    ctx.destroy(random);
    ctx.destroy(image);
}

void overlay_and_pixel_grid() {
    auto ctx = Context::create();
    auto gpu = webgpu::native_context(ctx);
    auto presenter = webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA8Unorm);
    constexpr std::uint32_t w = 6, h = 4;
    auto image = ctx.create_image({w, h});
    auto mask = ctx.create_mask({w, h});
    const auto pixels = random_pixels(w * h, 4, true);
    std::vector<std::uint8_t> coverage(w * h);
    for (std::size_t i = 0; i < coverage.size(); ++i) {
        coverage[i] = std::array<std::uint8_t, 4>{0, 255, 128, 64}[i % 4];
    }
    upload(ctx, image, pixels);
    upload(ctx, mask, coverage);
    Target target(gpu, 3 * w, 3 * h);
    for (bool selected : {false, true}) {
        const webgpu::ViewportOptions options{.view = Affine::scale(3),
                                              .overlay = &mask,
                                              .overlay_color = {0, 0.4f, 0, 0.4f},
                                              .overlay_selected = selected};
        ctx.wait(reserved_draw(presenter, image, target.view.value, 3 * w, 3 * h, options));
        pixels_near(target.read(gpu), expected(3 * w, 3 * h, [&](auto x, auto y) {
                        return magnified(pixels, w, h, options, x, y, coverage);
                    }));
    }
    // Zoomed out, the overlay averages coverage like the image.
    Target half(gpu, w / 2, h / 2);
    const std::array<std::uint8_t, 2> columns{0, 255};
    for (std::size_t i = 0; i < coverage.size(); ++i) {
        coverage[i] = columns[i % 2];
    }
    upload(ctx, mask, coverage);
    auto cmd = ctx.create_commands(1);
    cmd.fill(image, {.color = {0, 0, 0, 1}});
    ctx.submit_and_wait(cmd);
    ctx.wait(reserved_draw(presenter, image, half.view.value, w / 2, h / 2,
                           {.view = Affine::scale(0.5f), .overlay = &mask}));
    pixels_near(half.read(gpu), expected(w / 2, h / 2, [&](auto, auto) {
                    return over({0.25f, 0, 0, 0.25f}, {0, 0, 0, 1});
                }));

    // Pixel grid: interior lines between image pixels above the zoom threshold only.
    const Color line{0, 0, 0.5f, 0.5f};
    for (float zoom : {8.0f, 4.0f}) {
        const auto tw = std::uint32_t(w * zoom), th = std::uint32_t(h * zoom);
        Target grid(gpu, tw, th);
        ctx.wait(reserved_draw(
            presenter, image, grid.view.value, tw, th,
            {.view = Affine::scale(zoom), .pixel_grid = true, .pixel_grid_color = line}));
        pixels_near(grid.read(gpu), expected(tw, th, [&](std::uint32_t x, std::uint32_t y) {
                        const auto boundary = [&](std::uint32_t t, std::uint32_t extent) {
                            const auto next = t + 1;
                            return zoom > 5 && next % std::uint32_t(zoom) == 0 &&
                                   next / std::uint32_t(zoom) < extent;
                        };
                        const Rgba black{0, 0, 0, 1};
                        return boundary(x, w) || boundary(y, h) ? over(rgba(line), black) : black;
                    }));
    }
    ctx.destroy(mask);
    ctx.destroy(image);
}

void reservation_contracts() {
    auto ctx = Context::create();
    auto presenter = webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA8Unorm);
    auto image = ctx.create_image({8, 8});
    auto mask = ctx.create_mask({8, 8});
    Target target(webgpu::native_context(ctx), 8, 8);
    const webgpu::ViewportOptions options{.view = Affine::scale(.0625f, .25f), .overlay = &mask};
    const auto need = webgpu::viewport_requirements(image.size(), options);
    // 8x8 -> 4x4, 2x2, 1x1: 21 pyramid pixels. The anisotropic level is 1x4.
    test::check(need.image_pyramid == 336 && need.mask_pyramid == 84 && need.image_axis == 64 &&
                    need.mask_axis == 16 && need.total == 500,
                "viewport requirements must report exact cache capacities");
    test::check(webgpu::viewport_requirements(image.size(), {}).total == 0,
                "unit zoom needs no large cache");
    const auto empty = ctx.memory();
    test::error(ErrorCode::capacity, "presenter.draw", "options",
                [&] { (void)presenter.draw(image, target.view.value, 8, 8, options); });
    test::check(ctx.memory().total == empty.total,
                "unreserved draw must fail without GPU allocation");
    presenter.reserve(image.size(), options);
    test::check(ctx.memory().presentation == empty.presentation + need.total,
                "reserved cache bytes must be visible in presentation memory");
    const auto reserved = ctx.memory();
    auto shown = presenter.draw(image, target.view.value, 8, 8, options);
    test::check(ctx.memory().total == reserved.total, "draw grew a reserved cache");
    ctx.wait(shown);
    // Growing 16x16 first needs a 1360-byte image pyramid, then a 340-byte mask
    // pyramid. Reject the second allocation and retain all four old capacities.
    ctx.set_memory_limit(reserved.total + 1360 + 339);
    test::error(ErrorCode::capacity, "presenter.reserve", "memory_limit",
                [&] { presenter.reserve({16, 16}, options); });
    test::check(ctx.memory().total == reserved.total,
                "failed reserve retained partial allocations");
    ctx.set_memory_limit(0);
    shown = presenter.draw(image, target.view.value, 8, 8, options);
    const auto larger = webgpu::viewport_requirements({16, 16}, options);
    presenter.reserve({16, 16}, options);
    test::check(ctx.memory().presentation == empty.presentation + need.total + larger.total,
                "reserve released buffers still referenced by an earlier submission");
    ctx.wait(shown);
    test::check(ctx.memory().presentation == empty.presentation + larger.total,
                "retirement must release replaced cache buffers");
    presenter.reserve({1, 1}, {});
    test::check(ctx.memory().presentation == empty.presentation + larger.total,
                "reserve must preserve existing larger capacities");
}

void contracts_and_resources() {
    auto ctx = Context::create();
    auto gpu = webgpu::native_context(ctx);
    auto presenter = webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA8Unorm);
    auto image = ctx.create_image({8, 8});
    auto mask = ctx.create_mask({8, 8});
    auto small = ctx.create_mask({4, 4});
    Target target(gpu, 8, 8);
    const auto draw = [&](const webgpu::ViewportOptions& options) {
        return presenter.draw(image, target.view.value, 8, 8, options);
    };
    using enum ErrorCode;
    test::error(invalid_argument, "presenter.draw", "view",
                [&] { (void)draw({.view = Affine::scale(0)}); });
    test::error(invalid_argument, "presenter.draw", "view", [&] {
        (void)draw({.view = Affine::translate(std::numeric_limits<float>::infinity(), 0)});
    });
    test::error(invalid_argument, "presenter.draw", "background",
                [&] { (void)draw({.background = {0, 0, 0, 2}}); });
    test::error(invalid_argument, "presenter.draw", "checker_second",
                [&] { (void)draw({.checker_second = {NAN, 0, 0, 1}}); });
    test::error(invalid_argument, "presenter.draw", "overlay_color",
                [&] { (void)draw({.overlay_color = {0, 0, 0, -1}}); });
    test::error(invalid_argument, "presenter.draw", "pixel_grid_zoom",
                [&] { (void)draw({.pixel_grid_zoom = -1}); });
    test::error(invalid_argument, "presenter.draw", "overlay",
                [&] { (void)draw({.overlay = &small}); });
    const Mask empty;
    test::error(invalid_resource, "presenter.draw", "overlay",
                [&] { (void)draw({.overlay = &empty}); });
    auto cmd = ctx.create_commands(2);
    cmd.fill(mask, {.coverage = 1});
    test::error(resource_busy, "presenter.draw", "overlay",
                [&] { (void)draw({.overlay = &mask}); });
    ctx.submit_and_wait(cmd);
    auto shown = draw({.view = Affine::scale(0.5f), .overlay = &mask});
    test::error(resource_busy, "destroy", "resource", [&] { ctx.destroy(mask); });
    ctx.wait(shown);

    // Repeated viewport draws reuse cached pipelines, uniforms and pyramid storage.
    auto other = ctx.create_image({8, 8});
    ctx.wait(draw({.view = Affine::scale(0.5f), .overlay = &mask}));
    WGPUGlobalReport before{}, after{};
    wgpuGenerateReport(gpu.instance, &before);
    for (int frame = 0; frame < 40; ++frame) {
        const auto& source = frame % 2 ? other : image;
        ctx.wait(reserved_draw(presenter, source, target.view.value, 8, 8,
                               {.view = Affine::scale(frame % 3 ? 0.4f : 2.0f), .overlay = &mask}));
    }
    wgpuGenerateReport(gpu.instance, &after);
    for (auto member :
         {&WGPUHubReport::buffers, &WGPUHubReport::bindGroups, &WGPUHubReport::commandBuffers,
          &WGPUHubReport::renderPipelines, &WGPUHubReport::computePipelines}) {
        const auto& a = after.hub.*member;
        const auto& b = before.hub.*member;
        test::check(a.numKeptFromUser == b.numKeptFromUser && a.numAllocated == b.numAllocated,
                    "repeated viewport draws retained native resources");
    }
    // Display forwards viewport options to its presenter.
    auto display = webgpu::Display::create(ctx, 8, 8);
    display.draw(image, {.view = Affine::scale(2), .pixel_grid = true});
    display.wait();
    display.close();
    ctx.destroy(other);
    ctx.destroy(small);
    ctx.destroy(mask);
    ctx.destroy(image);
}
// Opaque image from a per-pixel linear gray value.
std::vector<float> gray_image(std::uint32_t w, std::uint32_t h,
                              const std::function<float(std::uint32_t, std::uint32_t)>& value) {
    std::vector<float> result;
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const float v = value(x, y);
            result.insert(result.end(), {v, v, v, 1});
        }
    }
    return result;
}

std::vector<std::uint8_t> draw_view(Context& ctx, webgpu::Presenter& presenter, const Image& image,
                                    std::uint32_t w, std::uint32_t h, Affine view) {
    auto gpu = webgpu::native_context(ctx);
    Target target(gpu, w, h);
    ctx.wait(reserved_draw(presenter, image, target.view.value, w, h, {.view = view}));
    return target.read(gpu);
}

void anisotropic_and_odd_sizes() {
    auto ctx = Context::create();
    auto presenter = webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA8Unorm);
    // Squeezing only one axis must keep the other axis' detail: stripes along the
    // unchanged axis stay pure black and white.
    for (bool rows : {true, false}) {
        auto image = ctx.create_image({8, 8});
        upload(ctx, image,
               gray_image(8, 8, [&](auto x, auto y) { return float((rows ? y : x) % 2); }));
        const auto w = rows ? 2u : 8u, h = rows ? 8u : 2u;
        const auto bytes = draw_view(ctx, presenter, image, w, h,
                                     rows ? Affine::scale(0.25f, 1) : Affine::scale(1, 0.25f));
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                test::check(bytes[(y * w + x) * 4] == ((rows ? y : x) % 2 ? 255 : 0),
                            "anisotropic zoom blurred the unscaled axis");
            }
        }
        ctx.destroy(image);
    }
    // Odd sizes: a 3 x 3 image shown as one pixel is the mean of its nine pixels, one third
    // white, whichever column holds the white; the partial pyramid texel keeps its true area.
    auto odd = ctx.create_image({3, 3});
    for (std::uint32_t white = 0; white < 3; ++white) {
        upload(ctx, odd, gray_image(3, 3, [&](auto x, auto) { return x == white ? 1.0f : 0.0f; }));
        const auto bytes = draw_view(ctx, presenter, odd, 1, 1, Affine::scale(1 / 3.0f));
        test::check(bytes[0] == test::channel(1 / 3.0f), "odd-size reduction is biased");
    }
    ctx.destroy(odd);
}

void box_means_above_workgroups() {
    auto ctx = Context::create();
    auto presenter = webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA8Unorm);
    // Power-of-two block zooms: every target pixel is the arithmetic mean of its image block,
    // computed here by direct summation. Pyramid levels span several 8 x 8 workgroups.
    constexpr std::uint32_t w = 48, h = 40;
    auto image = ctx.create_image({w, h});
    const auto pixels = random_pixels(w * h, 9, true);
    upload(ctx, image, pixels);
    for (auto [bx, by] : {std::pair{2u, 2u}, std::pair{4u, 4u}, std::pair{8u, 8u},
                          std::pair{8u, 2u}, std::pair{1u, 8u}, std::pair{16u, 8u}}) {
        const auto tw = w / bx, th = h / by;
        const auto bytes =
            draw_view(ctx, presenter, image, tw, th, Affine::scale(1.0f / bx, 1.0f / by));
        pixels_near(bytes, expected(tw, th, [&](auto x, auto y) {
                        Rgba sum{};
                        for (std::uint32_t dy = 0; dy < by; ++dy) {
                            for (std::uint32_t dx = 0; dx < bx; ++dx) {
                                const auto p = pixel(pixels, w, x * bx + dx, y * by + dy);
                                for (std::size_t c = 0; c < 4; ++c) {
                                    sum[c] += p[c] / float(bx * by);
                                }
                            }
                        }
                        return sum;
                    }));
    }
    // Non-power-of-two zoom of a linear ramp: a box mean of a ramp is its value at the box
    // center, (3x + 1.5) / w for 3-pixel boxes, known in closed form.
    auto ramp = ctx.create_image({w, 8});
    upload(ctx, ramp, gray_image(w, 8, [&](auto x, auto) { return (x + 0.5f) / w; }));
    const auto bytes = draw_view(ctx, presenter, ramp, w / 3, 2, Affine::scale(1 / 3.0f, 0.25f));
    pixels_near(bytes, expected(w / 3, 2, [&](auto x, auto) {
                    const float v = (3 * x + 1.5f) / w;
                    return Rgba{v, v, v, 1};
                }));
    ctx.destroy(ramp);
    ctx.destroy(image);
}

void hdr_minification() {
    auto ctx = Context::create();
    auto presenter = webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA8Unorm);
    // Halves of +100000 and -99999 average to 0.5 (sRGB byte 188); clipping any level to the
    // half-float range, or to display range, destroys that difference.
    for (std::uint32_t size : {4u, 32u}) {
        auto image = ctx.create_image({size, size});
        std::vector<float> pixels;
        for (std::uint32_t i = 0; i < size * size; ++i) {
            pixels.insert(pixels.end(), {i < size * size / 2 ? 100000.0f : -99999.0f, 0, 0, 1});
        }
        upload(ctx, image, pixels);
        const auto bytes = draw_view(ctx, presenter, image, 1, 1, Affine::scale(1.0f / size));
        test::check(bytes[0] == test::channel(0.5f), "HDR pyramid levels were clipped");
        ctx.destroy(image);
    }
}
void extreme_anisotropy() {
    auto ctx = Context::create();
    auto presenter = webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA8Unorm);
    // Stripes along the unscaled axis survive any squeeze ratio, including 16:1 (the
    // reviewer's 32 x 8 case) and 512:1, along either axis.
    for (auto [length, ratio] : {std::pair{32u, 16u}, std::pair{1024u, 512u}}) {
        for (bool rows : {true, false}) {
            const auto w = rows ? length : 8u, h = rows ? 8u : length;
            auto image = ctx.create_image({w, h});
            upload(ctx, image,
                   gray_image(w, h, [&](auto x, auto y) { return float((rows ? y : x) % 2); }));
            const auto tw = rows ? length / ratio : 8u, th = rows ? 8u : length / ratio;
            const auto bytes =
                draw_view(ctx, presenter, image, tw, th,
                          rows ? Affine::scale(1.0f / ratio, 1) : Affine::scale(1, 1.0f / ratio));
            for (std::uint32_t y = 0; y < th; ++y) {
                for (std::uint32_t x = 0; x < tw; ++x) {
                    test::check(bytes[(y * tw + x) * 4] == ((rows ? y : x) % 2 ? 255 : 0),
                                "squeeze " + std::to_string(ratio) + ":1 blurred the other axis");
                }
            }
            ctx.destroy(image);
        }
    }
    // Anisotropic zoom-out on both axes: each target pixel is the directly summed mean of
    // its bx x by block; the image spans many 8 x 8 workgroups.
    constexpr std::uint32_t w = 256, h = 64;
    auto image = ctx.create_image({w, h});
    const auto pixels = random_pixels(w * h, 12, false);
    upload(ctx, image, pixels);
    for (auto [bx, by] : {std::pair{64u, 2u}, std::pair{2u, 32u}, std::pair{128u, 4u},
                          std::pair{16u, 1u}, std::pair{256u, 8u}}) {
        const auto tw = w / bx, th = h / by;
        const webgpu::ViewportOptions options{.view = Affine::scale(1.0f / bx, 1.0f / by),
                                              .checker_size = 0};
        auto gpu = webgpu::native_context(ctx);
        Target target(gpu, tw, th);
        ctx.wait(reserved_draw(presenter, image, target.view.value, tw, th, options));
        pixels_near(target.read(gpu), expected(tw, th, [&](auto x, auto y) {
                        Rgba sum{};
                        for (std::uint32_t dy = 0; dy < by; ++dy) {
                            for (std::uint32_t dx = 0; dx < bx; ++dx) {
                                const auto p = pixel(pixels, w, x * bx + dx, y * by + dy);
                                for (std::size_t c = 0; c < 4; ++c) {
                                    sum[c] += p[c] / float(bx * by);
                                }
                            }
                        }
                        return over(sum, {1, 1, 1, 1});
                    }));
    }
    // The overlay follows the same anisotropic filtering: masked rows keep full tint.
    auto mask = ctx.create_mask({w, h});
    std::vector<std::uint8_t> coverage(w * h);
    for (std::uint32_t i = 0; i < w * h; ++i) {
        coverage[i] = (i / w) % 2 ? 255 : 0;
    }
    upload(ctx, mask, coverage);
    auto cmd = ctx.create_commands(1);
    cmd.fill(image, {.color = {0, 0, 0, 1}});
    ctx.submit_and_wait(cmd);
    auto gpu = webgpu::native_context(ctx);
    Target squeezed(gpu, 4, h);
    ctx.wait(reserved_draw(presenter, image, squeezed.view.value, 4, h,
                           {.view = Affine::scale(1.0f / 64, 1), .overlay = &mask}));
    pixels_near(squeezed.read(gpu), expected(4, h, [&](auto, auto y) {
                    return y % 2 ? Rgba{0, 0, 0, 1} : over({0.5f, 0, 0, 0.5f}, {0, 0, 0, 1});
                }));
    ctx.destroy(mask);
    ctx.destroy(image);
}

void hdr_box_normalization() {
    auto ctx = Context::create();
    auto presenter = webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA8Unorm);
    // Quadrants +1e38 and -1e38 over a 0.5 half: the mean is 0.25 (byte 137). Weighting by
    // raw areas before dividing overflows float32.
    for (std::uint32_t size : {4u, 64u}) {
        auto image = ctx.create_image({size, size});
        const auto half = size / 2;
        std::vector<float> pixels;
        for (std::uint32_t y = 0; y < size; ++y) {
            for (std::uint32_t x = 0; x < size; ++x) {
                const float v = y < half ? (x < half ? 1e38f : -1e38f) : 0.5f;
                pixels.insert(pixels.end(), {v, 0, 0, 1});
            }
        }
        upload(ctx, image, pixels);
        auto bytes = draw_view(ctx, presenter, image, 1, 1, Affine::scale(1.0f / size));
        test::check(bytes[0] == test::channel(0.25f),
                    "HDR box mean overflowed: " + std::to_string(bytes[0]));
        // The same through the anisotropic path.
        bytes = draw_view(ctx, presenter, image, 1, 2, Affine::scale(1.0f / size, 2.0f / size));
        test::check(bytes[0] == test::channel(0.0f) && bytes[4] == test::channel(0.5f),
                    "anisotropic HDR box mean differs");
        ctx.destroy(image);
    }
}

#ifdef WGPUPIXEL_TEST_FAILURE_INJECTION
void failed_cache_generation() {
    auto ctx = Context::create();
    auto gpu = webgpu::native_context(ctx);
    auto image = ctx.create_image({32, 16});
    auto mask = ctx.create_mask(image.size());
    ctx.run_and_wait([&](Commands& cmd) {
        cmd.fill(image, {.color = {1, 0, 0, 1}});
        cmd.fill(mask, {.coverage = 128.0f / 255});
    });
    // All four caches are exercised: isotropic pyramid/anisotropic reduction,
    // each for the image and the overlay. Partial green coverage leaves both caches
    // visible in the expected red/green result.
    for (const bool anisotropic : {false, true}) {
        const auto view = Affine::scale(1.0f / 16, anisotropic ? 1.0f / 4 : 1.0f / 16);
        const std::uint32_t height = anisotropic ? 4 : 1;
        Target target(gpu, 2, height);
        webgpu::ViewportOptions options{.view = view, .checker_first = {0, 0, 0, 0},
                                        .checker_second = {0, 0, 0, 0}, .overlay = &mask,
                                        .overlay_color = {0, 1, 0, 1}, .overlay_selected = true};
        for (const bool asynchronous : {false, true}) {
            auto presenter = webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA8Unorm);
            presenter.reserve(image.size(), options);
            {
                Native<WGPUCommandEncoder, wgpuCommandEncoderRelease> encoder{
                    wgpuDeviceCreateCommandEncoder(gpu.device, nullptr)};
                Native<WGPUCommandBuffer, wgpuCommandBufferRelease> empty{
                    wgpuCommandEncoderFinish(encoder.value, nullptr)};
                wgpuQueueSubmit(gpu.queue, 1, &empty.value);
                if (asynchronous) {
                    skip_submission = delay_completion = true;
                } else {
                    reject_with = empty.value;
                }
                const auto failed = presenter.draw(image, target.view.value, 2, height, options);
                if (asynchronous) {
                    // Report an asynchronous job failure after draw returned. No cache
                    // data was generated by the skipped submission.
                    delayed_completion.callback(WGPUQueueWorkDoneStatus_Error, {nullptr, 0},
                                                delayed_completion.userdata1,
                                                delayed_completion.userdata2);
                }
                test::error(ErrorCode::execution_failed, "wait", "", [&] { ctx.wait(failed); });
            } // Also drop the failure token: cache validity must survive its lifetime.
            ctx.wait(presenter.draw(image, target.view.value, 2, height, options));
            pixels_near(target.read(gpu), expected(2, height, [](auto, auto) {
                            return Rgba{127.0f / 255, 128.0f / 255, 0, 1};
                        }));
            // A third draw should continue using the repaired generation successfully.
            ctx.wait(presenter.draw(image, target.view.value, 2, height, options));
            pixels_near(target.read(gpu), expected(2, height, [](auto, auto) {
                            return Rgba{127.0f / 255, 128.0f / 255, 0, 1};
                        }));
        }
    }
}
#endif
} // namespace

int main() {
#ifdef WGPUPIXEL_TEST_FAILURE_INJECTION
    test::run("failed viewport generations rebuild image and mask caches", failed_cache_generation);
#endif
    test::run("viewport reserve is explicit, atomic and retains pending caches",
              reservation_contracts);
    test::run("viewport at 100% composites over the checkerboard for unorm and sRGB targets",
              identity_and_transparency);
    test::run("viewport magnification shows nearest pixels, pans, rotates and fills background",
              magnification_and_background);
    test::run("viewport minification averages areas and follows image revisions",
              minification_averages_area);
    test::run("viewport selection overlay and pixel grid", overlay_and_pixel_grid);
    test::run("viewport contracts and stable native resources", contracts_and_resources);
    test::run("viewport zooming out one axis keeps the other; odd sizes are unbiased",
              anisotropic_and_odd_sizes);
    test::run("viewport zoom-out equals directly summed box means across workgroups",
              box_means_above_workgroups);
    test::run("viewport zoom-out keeps HDR averages", hdr_minification);
    test::run("viewport keeps detail at extreme anisotropy and sums anisotropic blocks",
              extreme_anisotropy);
    test::run("viewport box means stay finite near the float limit", hdr_box_normalization);
    return test::finish();
}
