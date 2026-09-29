#include "test.h"
#include <wgpupixel_webgpu.h>
#include <webgpu/wgpu.h>
#include <array>
#include <utility>

namespace {
using namespace wgpupixel;

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

    // Readback belongs to the pixel oracle only; Presenter never downloads pixels.
    std::vector<std::uint8_t> read(webgpu::NativeContext gpu) const {
        const auto stride = (width * 4 + 255) & ~std::uint32_t{255};
        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        desc.size = std::uint64_t(stride) * height;
        Native<WGPUBuffer, wgpuBufferRelease> buffer{wgpuDeviceCreateBuffer(gpu.device, &desc)};
        test::check(buffer.value != nullptr, "readback allocation failed");
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

void pixels_near(std::span<const std::uint8_t> actual, std::span<const std::uint8_t> expected) {
    test::check(actual.size() == expected.size(), "pixel byte counts differ");
    for (std::size_t i = 0; i < actual.size(); ++i) {
        test::near(actual[i], expected[i], 1.0f);
    }
}

void color_and_order() {
    auto ctx = Context::create();
    auto gpu = webgpu::native_context(ctx);
    auto presenter = webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA8Unorm);
    auto image = ctx.create_image({2, 2});
    auto upload = ctx.create_upload_buffer(image);
    const std::array<std::uint8_t, 16> pixels{255, 0, 0,   255, 0,  128, 0,   255,
                                              0,   0, 255, 255, 37, 100, 200, 255};
    ctx.write(upload, pixels);
    auto cmd = ctx.create_commands(1);
    cmd.upload(upload, image);
    auto computed = ctx.submit(cmd);
    Target target(gpu, 2, 2);
    // Queue ordering must make the compute result visible without an intervening wait.
    auto displayed = presenter.draw(image, target.view.value, 2, 2);
    test::error(ErrorCode::resource_busy, "destroy", "resource", [&] { ctx.destroy(image); });
    ctx.wait(displayed);
    ctx.wait(computed);
    test::check(image.revision() == 1, "presentation must not change image revision");
    pixels_near(target.read(gpu), pixels);
    ctx.destroy(upload);
    ctx.destroy(image);
}

void scaling_and_hdr() {
    auto ctx = Context::create();
    auto gpu = webgpu::native_context(ctx);
    auto presenter = webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA8Unorm);
    auto image = ctx.create_image({2, 1});
    const std::array<float, 8> values{0, 0, 0, 1, 1, 1, 1, 1};
    test::paint(ctx, image, values);
    Target scaled(gpu, 3, 1);
    ctx.wait(presenter.draw(image, scaled.view.value, 3, 1));
    const std::array<std::uint8_t, 12> expected{0,   0,   0,   255, 188, 188,
                                                188, 255, 255, 255, 255, 255};
    pixels_near(scaled.read(gpu), expected);
    auto cmd = ctx.create_commands(1);
    cmd.fill(image, {.color = {2.0f, -0.5f, 0.25f, 1.0f}});
    auto computed = ctx.submit(cmd);
    ctx.wait(presenter.draw(image, scaled.view.value, 3, 1));
    ctx.wait(computed);
    const std::array<std::uint8_t, 12> clipped{255, 0,   137, 255, 255, 0,
                                               137, 255, 255, 0,   137, 255};
    pixels_near(scaled.read(gpu), clipped);
    // Presentation must not clamp or alter the processing buffer.
    cmd.brightness(image, {.amount = -1.0f});
    ctx.submit_and_wait(cmd);
    const auto retained = test::read(ctx, image);
    test::check(retained[0] == 255, "presentation clipped the HDR source in place");
    ctx.destroy(image);
}

void alpha_and_formats() {
    auto ctx = Context::create();
    auto gpu = webgpu::native_context(ctx);
    auto image = ctx.create_image({2, 1});
    auto upload = ctx.create_upload_buffer(image);
    const std::array<std::uint8_t, 8> pixels{240, 100, 20, 128, 255, 255, 255, 0};
    ctx.write(upload, pixels);
    auto cmd = ctx.create_commands(1);
    cmd.upload(upload, image);
    ctx.submit_and_wait(cmd);
    for (auto format : {WGPUTextureFormat_RGBA8Unorm, WGPUTextureFormat_BGRA8Unorm,
                        WGPUTextureFormat_RGBA8UnormSrgb, WGPUTextureFormat_BGRA8UnormSrgb}) {
        auto presenter = webgpu::Presenter::create(ctx, format);
        Target target(gpu, 2, 1, format);
        ctx.wait(presenter.draw(image, target.view.value, 2, 1));
        std::array<std::uint8_t, 8> expected{120, 50, 10, 128, 0, 0, 0, 0};
        if (format == WGPUTextureFormat_BGRA8Unorm || format == WGPUTextureFormat_BGRA8UnormSrgb) {
            std::swap(expected[0], expected[2]);
        }
        pixels_near(target.read(gpu), expected);
    }
    ctx.destroy(upload);
    ctx.destroy(image);
}

void contracts() {
    Context empty;
    test::error(ErrorCode::invalid_resource, "native_context", "context",
                [&] { (void)webgpu::native_context(empty); });
    test::error(ErrorCode::invalid_resource, "presenter.create", "context",
                [&] { (void)webgpu::Presenter::create(empty, WGPUTextureFormat_RGBA8Unorm); });
    auto ctx = Context::create();
    auto gpu = webgpu::native_context(ctx);
    test::check(gpu.device && gpu.queue && gpu.adapter && gpu.instance,
                "native context has missing handles");
    auto image = ctx.create_image({2, 2});
    auto presenter = webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA8Unorm);
    Target target(gpu, 2, 2);
    test::error(ErrorCode::invalid_argument, "presenter.create", "format",
                [&] { (void)webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA16Float); });
    test::error(ErrorCode::invalid_argument, "presenter.draw", "target",
                [&] { (void)presenter.draw(image, nullptr, 2, 2); });
    for (auto size : {std::pair{0u, 2u}, std::pair{2u, 0u}, std::pair{UINT32_MAX, 2u}}) {
        test::error(size.first == UINT32_MAX ? ErrorCode::capacity : ErrorCode::invalid_argument,
                    "presenter.draw", size.second == 0 ? "height" : "width", [&] {
            (void)presenter.draw(image, target.view.value, size.first, size.second);
        });
    }
    test::error(ErrorCode::invalid_resource, "presenter.draw", "image",
                [&] { (void)presenter.draw(Image{}, target.view.value, 2, 2); });
    auto other = Context::create();
    auto foreign = other.create_image({2, 2});
    test::error(ErrorCode::invalid_resource, "presenter.draw", "image",
                [&] { (void)presenter.draw(foreign, target.view.value, 2, 2); });
    other.destroy(foreign);
    Target wrong_format(gpu, 2, 2, WGPUTextureFormat_BGRA8Unorm);
    test::error(ErrorCode::execution_failed, "presenter.draw", "",
                [&] { (void)presenter.draw(image, wrong_format.view.value, 2, 2); });
    test::error(ErrorCode::execution_failed, "presenter.draw", "",
                [&] { (void)presenter.draw(image, target.view.value, 3, 2); });
    auto cmd = ctx.create_commands(1);
    cmd.fill(image, {.color = {0.5f, 0.25f, 0.125f, 1.0f}});
    test::error(ErrorCode::resource_busy, "presenter.draw", "image",
                [&] { (void)presenter.draw(image, target.view.value, 2, 2); });
    auto computed = ctx.submit(cmd);
    auto moved = std::move(presenter);
    test::error(ErrorCode::invalid_resource, "presenter.draw", "presenter",
                [&] { (void)presenter.draw(image, target.view.value, 2, 2); });
    auto shown = moved.draw(image, target.view.value, 2, 2);
    moved = {};
    ctx.wait(shown); // Destroying the presenter must not invalidate an in-flight draw.
    ctx.wait(computed);
    ctx.destroy(image);
    presenter = webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA8Unorm);
    test::error(ErrorCode::invalid_resource, "presenter.draw", "image",
                [&] { (void)presenter.draw(image, target.view.value, 2, 2); });
}

void repeated_draws() {
    auto ctx = Context::create();
    auto gpu = webgpu::native_context(ctx);
    auto presenter = webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA8Unorm);
    auto small = ctx.create_image({1, 1});
    auto wide = ctx.create_image({2, 1});
    auto cmd = ctx.create_commands(2);
    cmd.fill(small, {.color = {1, 0, 0, 1}});
    cmd.fill(wide, {.color = {0, 1, 0, 1}});
    ctx.submit_and_wait(cmd);
    Target first(gpu, 2, 2), second(gpu, 2, 2);
    WGPUGlobalReport before{}, after{};
    wgpuGenerateReport(gpu.instance, &before);
    for (int frame = 0; frame < 100; ++frame) {
        auto a = presenter.draw(small, first.view.value, 2, 2);
        auto b = presenter.draw(wide, second.view.value, 2, 2);
        ctx.wait(b);
        ctx.wait(a);
    }
    wgpuGenerateReport(gpu.instance, &after);
    for (auto member : {&WGPUHubReport::buffers, &WGPUHubReport::bindGroups,
                        &WGPUHubReport::commandBuffers, &WGPUHubReport::renderPipelines,
                        &WGPUHubReport::textures, &WGPUHubReport::textureViews}) {
        const auto& a = after.hub.*member;
        const auto& b = before.hub.*member;
        test::check(a.numKeptFromUser == b.numKeptFromUser && a.numAllocated == b.numAllocated,
                    "repeated draws retained native resources");
    }
    const std::array<std::uint8_t, 16> red{255, 0, 0, 255, 255, 0, 0, 255,
                                           255, 0, 0, 255, 255, 0, 0, 255};
    const std::array<std::uint8_t, 16> green{0, 255, 0, 255, 0, 255, 0, 255,
                                             0, 255, 0, 255, 0, 255, 0, 255};
    pixels_near(first.read(gpu), red);
    pixels_near(second.read(gpu), green);
    ctx.destroy(small);
    ctx.destroy(wide);
}

void deferred_device_and_diagnostics() {
    auto ctx = Context::create();
    auto gpu = webgpu::native_context(ctx);
    auto presenter = webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA8Unorm);
    test::check(gpu.device && gpu.queue, "deferred context must expose the UI device");
    ctx.prepare();
    test::check(webgpu::native_context(ctx).device == gpu.device,
                "preparation must retain the same GPU device");

    auto image = ctx.create_image({1, 1});
    auto readback = ctx.create_readback_buffer(image);
    auto commands = ctx.create_commands(2);
    commands.fill(image, {.color = {1, 0, 0, 1}});
    commands.download(image, readback);
    auto done = ctx.submit(commands);

    // An external renderer error must retain WebGPU's diagnostic, not just its code.
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.label = {"diagnostic-sentinel", WGPU_STRLEN};
    descriptor.size = 16;
    descriptor.usage = WGPUBufferUsage_None;
    Native<WGPUBuffer, wgpuBufferRelease> invalid{wgpuDeviceCreateBuffer(gpu.device, &descriptor)};
    wgpuDevicePoll(gpu.device, true, nullptr);
    bool reported = false;
    try {
        ctx.prepare();
    } catch (const Error& error) {
        reported =
            error.code() == ErrorCode::execution_failed &&
            std::string_view(error.what()).find("diagnostic-sentinel") != std::string_view::npos;
    }
    test::check(reported, "uncaptured GPU error must preserve the backend diagnostic");
    std::string message;
    for (int attempt = 0; attempt < 2; ++attempt) {
        bool failed = false;
        try {
            ctx.wait(done);
        } catch (const Error& error) {
            failed = error.code() == ErrorCode::execution_failed && error.operation() == "wait" &&
                     std::string_view(error.what()).find("diagnostic-sentinel") !=
                         std::string_view::npos;
            if (attempt) {
                test::check(message == error.what(),
                            "repeated waits must retain the same diagnostic");
            }
            message = error.what();
        }
        test::check(failed, "wait must preserve the uncaptured backend diagnostic");
    }
    std::array<std::uint8_t, 4> pixels{7, 7, 7, 7};
    test::error(ErrorCode::execution_failed, "read", "", [&] { ctx.read(readback, pixels); });
    test::check(pixels == std::array<std::uint8_t, 4>{7, 7, 7, 7},
                "failed submission output must remain inaccessible");
    ctx.destroy(readback);
    ctx.destroy(image); // wait retired resources even though it reported failure.
}
} // namespace

int main() {
    test::run("presentation color conversion, orientation, compute ordering, and lifetime",
              color_and_order);
    test::run("presentation linear interpolation and non-destructive SDR clipping",
              scaling_and_hdr);
    test::run("presentation premultiplied alpha and RGBA/BGRA linear/sRGB targets",
              alpha_and_formats);
    test::run("presentation contracts and pending presenter destruction", contracts);
    test::run("repeated presentation preserves queue ordering and native resource counts",
              repeated_draws);
    test::run("deferred device sharing and retained uncaptured diagnostics",
              deferred_device_and_diagnostics);
    return test::finish();
}
