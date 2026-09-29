#include "../viewport_target.h"
#include <limits>
#include <random>

using namespace wgpupixel;
using test::Target;
using webgpu::DirtyHint;

#ifdef WGPUPIXEL_TEST_DISPATCH_COUNTS
namespace {
std::uint64_t dispatched_groups = 0;
bool skip_draw = false, hold_callback = false;
std::vector<WGPUBuffer> allocated_caches;
WGPUQueueWorkDoneCallbackInfo held_callback = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
}
// Test-only CopySrc usage exposes the exact float bits without a public cache API.
extern "C" WGPUBuffer __real_wgpuDeviceCreateBuffer(WGPUDevice, const WGPUBufferDescriptor*);
extern "C" WGPUBuffer __wrap_wgpuDeviceCreateBuffer(WGPUDevice device,
                                                    const WGPUBufferDescriptor* descriptor) {
    auto copy = *descriptor;
    const bool cache = copy.label.data &&
        std::string_view(copy.label.data, copy.label.length) == "presenter.reserve";
    if (cache) copy.usage |= WGPUBufferUsage_CopySrc;
    auto buffer = __real_wgpuDeviceCreateBuffer(device, &copy);
    if (cache) allocated_caches.push_back(buffer);
    return buffer;
}
extern "C" WGPUSubmissionIndex __real_wgpuQueueSubmitForIndex(
    WGPUQueue, std::size_t, const WGPUCommandBuffer*);
extern "C" WGPUSubmissionIndex __wrap_wgpuQueueSubmitForIndex(
    WGPUQueue queue, std::size_t count, const WGPUCommandBuffer* commands) {
    if (std::exchange(skip_draw, false)) return 0;
    return __real_wgpuQueueSubmitForIndex(queue, count, commands);
}
extern "C" WGPUFuture __real_wgpuQueueOnSubmittedWorkDone(WGPUQueue, WGPUQueueWorkDoneCallbackInfo);
extern "C" WGPUFuture __wrap_wgpuQueueOnSubmittedWorkDone(WGPUQueue queue, WGPUQueueWorkDoneCallbackInfo info) {
    if (std::exchange(hold_callback, false)) {
        held_callback = info;
        return {};
    }
    return __real_wgpuQueueOnSubmittedWorkDone(queue, info);
}
extern "C" void __real_wgpuComputePassEncoderDispatchWorkgroups(WGPUComputePassEncoder,
                                                               std::uint32_t, std::uint32_t, std::uint32_t);
extern "C" void __wrap_wgpuComputePassEncoderDispatchWorkgroups(WGPUComputePassEncoder pass,
                                                               std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    dispatched_groups += std::uint64_t(x) * y * z;
    __real_wgpuComputePassEncoderDispatchWorkgroups(pass, x, y, z);
}
#endif

namespace {
struct CacheReadback {
#ifdef WGPUPIXEL_TEST_DISPATCH_COUNTS
    std::array<WGPUBuffer, 4> buffers{};
    std::array<std::uint64_t, 4> capacities{};

    static std::array<std::uint64_t, 4> sizes(ImageSize size, const webgpu::ViewportOptions& options) {
        const auto r = webgpu::viewport_requirements(size, options);
        return {r.image_pyramid, r.mask_pyramid, r.image_axis, r.mask_axis};
    }

    std::vector<std::uint32_t> read(webgpu::NativeContext gpu, int slot, std::uint64_t bytes) const {
        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        desc.size = bytes;
        test::Native<WGPUBuffer, wgpuBufferRelease> buffer{wgpuDeviceCreateBuffer(gpu.device, &desc)};
        test::Native<WGPUCommandEncoder, wgpuCommandEncoderRelease> encoder{
            wgpuDeviceCreateCommandEncoder(gpu.device, nullptr)};
        wgpuCommandEncoderCopyBufferToBuffer(encoder.value, buffers[slot], 0, buffer.value, 0, bytes);
        test::Native<WGPUCommandBuffer, wgpuCommandBufferRelease> commands{
            wgpuCommandEncoderFinish(encoder.value, nullptr)};
        wgpuQueueSubmit(gpu.queue, 1, &commands.value);
        WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Error;
        WGPUBufferMapCallbackInfo callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowProcessEvents;
        callback.userdata1 = &status;
        callback.callback = [](WGPUMapAsyncStatus result, WGPUStringView, void* data, void*) {
            *static_cast<WGPUMapAsyncStatus*>(data) = result;
        };
        wgpuBufferMapAsync(buffer.value, WGPUMapMode_Read, 0, bytes, callback);
        wgpuDevicePoll(gpu.device, true, nullptr);
        test::check(status == WGPUMapAsyncStatus_Success, "cache readback mapping failed");
        const auto* words = static_cast<const std::uint32_t*>(
            wgpuBufferGetConstMappedRange(buffer.value, 0, bytes));
        std::vector<std::uint32_t> bits(words, words + bytes / 4);
        wgpuBufferUnmap(buffer.value);
        return bits;
    }
#endif

    void reserve(webgpu::Presenter& presenter, ImageSize size, const webgpu::ViewportOptions& options) {
#ifdef WGPUPIXEL_TEST_DISPATCH_COUNTS
        allocated_caches.clear();
#endif
        presenter.reserve(size, options);
#ifdef WGPUPIXEL_TEST_DISPATCH_COUNTS
        const auto needed = sizes(size, options);
        std::size_t next = 0;
        for (int i = 0; i < 4; ++i) {
            if (needed[i] <= capacities[i]) continue;
            buffers[i] = allocated_caches.at(next++);
            capacities[i] = needed[i];
        }
        test::check(next == allocated_caches.size(), "unexpected cache allocation");
#endif
    }

    void compare(const CacheReadback& other, webgpu::NativeContext gpu, ImageSize size,
                 const webgpu::ViewportOptions& options) const {
#ifdef WGPUPIXEL_TEST_DISPATCH_COUNTS
        const auto needed = sizes(size, options);
        for (int i = 0; i < 4; ++i) {
            if (!needed[i]) continue;
            test::check(read(gpu, i, needed[i]) == other.read(gpu, i, needed[i]),
                        "float cache bits differ from full rebuild in slot " + std::to_string(i));
        }
#endif
    }
};

struct Comparison {
    Context ctx = Context::create();
    webgpu::NativeContext gpu = webgpu::native_context(ctx);
    Image image, reference;
    Mask mask, reference_mask;
    webgpu::Presenter partial = webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA8Unorm);
    webgpu::Presenter full = webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA8Unorm);
    Target actual{gpu, 176, 144}, expected{gpu, 176, 144};
    std::optional<DirtyHint> dirty_hint, overlay_dirty_hint;
    CacheReadback partial_cache, full_cache;

    Comparison(ImageSize size) : image(ctx.create_image(size)), reference(ctx.create_image(size)),
                                 mask(ctx.create_mask(size)), reference_mask(ctx.create_mask(size)) {
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.checkerboard(image, {.size = 3, .first = {0.07f, 0.2f, -0.1f, 0.4f},
                                    .second = {1.3f, 0.1f, 0.3f, 0.8f}});
            cmd.fill(mask, {.coverage = 0.43f});
        });
    }

    webgpu::ViewportOptions options(float scale = 0.125f, float degrees = 0,
                                     float anisotropy = 1) const {
        const auto size = image.size();
        return {.view = Affine::translate(88.5f, 72.5f) * Affine::rotate(degrees) *
                        Affine::scale(size.width == 1 ? 1 : scale,
                                      size.height == 1 ? 1 : scale * anisotropy) *
                        Affine::translate(-float(size.width) / 2, -float(size.height) / 2),
                .overlay = &mask, .overlay_color = {0.08f, 0.4f, 0.02f, 0.6f}};
    }

    void compare(webgpu::ViewportOptions options) {
        partial_cache.reserve(partial, image.size(), options);
        // Full copies advance the independent oracle's revisions on every comparison.
        auto cmd = ctx.create_commands();
        cmd.copy(image, reference);
        cmd.copy(mask, reference_mask);
        const auto copied = ctx.submit(cmd);
        const auto drawn = partial.draw(image, actual.view.value, actual.width, actual.height,
                                        options, dirty_hint, overlay_dirty_hint);
        if (options.overlay) options.overlay = &reference_mask;
        full_cache.reserve(full, reference.size(), options);
        ctx.wait(full.draw(reference, expected.view.value, expected.width, expected.height, options));
        ctx.wait(drawn);
        ctx.wait(copied);
        partial_cache.compare(full_cache, gpu, image.size(), options);
        test::check(actual.read(gpu) == expected.read(gpu), "dirty draw differs from full rebuild");
    }

    void edit(Rect image_region, Rect mask_region, int changes = 3, float value = 0.3f) {
        dirty_hint = DirtyHint{image_region, image.revision()};
        overlay_dirty_hint = DirtyHint{mask_region, mask.revision()};
        auto cmd = ctx.create_commands();
        if (changes & 1) cmd.fill(image, {.color = {value, -0.1f, 0.6f * value, 0.7f},
                                        .region = image_region});
        if (changes & 2) cmd.fill(mask, {.coverage = value / 2, .region = mask_region});
        // Deliberately no wait: draw must follow edits through queue ordering alone.
        (void)ctx.submit(cmd);
    }
};

Rect random_region(std::mt19937& rng, ImageSize size, int step) {
    const auto w = static_cast<int>(size.width), h = static_cast<int>(size.height);
    switch (step % 10) {
    case 0: return {0, 0, 1, 1};
    case 1: return {w - 1, h - 1, 1, 1};
    case 2: return {-3, -5, 9, 11};
    case 3: return {w - 2, h - 2, 17, 23};
    case 4: return {0, h / 2, w, 1};
    case 5: return {w / 2, 0, 1, h};
    case 6: return {std::numeric_limits<int>::max(), 0, std::numeric_limits<int>::max(), h};
    case 7: return {w / 2, h / 2, 0, 3};
    default: return {int(rng() % w), int(rng() % h), 1 + int(rng() % w), 1 + int(rng() % h)};
    }
}

void random_edits() {
    std::mt19937 rng(0x725ab);
    for (ImageSize size : {ImageSize{257, 193}, {256, 128}, {1, 129}, {131, 1}, {1, 1}}) {
        Comparison c(size);
        for (int step = 0; step < 64; ++step) {
            const auto a = random_region(rng, size, step);
            const auto b = random_region(rng, size, step + 3);
            c.edit(a, b, 1 + step % 3, float(1 + rng() % 19) / 10);
            // Repeated views exercise partial axis reductions; changed views exercise
            // rotations, both anisotropic axes, and a source-level axis.
            const float scale = std::array{0.25f, 0.125f, 0.0625f, 0.2f, 0.03125f, 0.0078125f,
                                            0.125f, 0.5f}[(step / 4) % 8];
            auto options = c.options(scale, std::array{0.0f, 23.0f, 90.0f, -37.0f}[(step / 16) % 4],
                                     std::array{1.0f, 4.0f, 0.25f, 1.0f}[(step / 4) % 4]);
            options.overlay_selected = step % 2;
            if (step % 13 == 0) c.dirty_hint.reset();
            if (step % 17 == 0) c.overlay_dirty_hint.reset();
            if (step % 19 == 0) options.overlay = nullptr;
            c.compare(options);
        }
    }
}

void cache_transitions() {
    Comparison c({129, 97});
    c.compare(c.options());
    const Rect a{0, 0, 51, 37}, b{110, 80, 19, 17};
    // An intervening draw consumes dirty information while bypassing some caches.
    for (int mode = 0; mode < 5; ++mode) {
        c.edit(a, a, 3, 0.2f * (mode + 1));
        auto bypass = c.options(mode == 0 ? 1 : 0.125f, 0, mode == 3 ? 4 : 1);
        if (mode == 1) bypass.overlay = nullptr;
        if (mode == 2) {
            c.ctx.wait(c.partial.draw(c.image, c.actual.view.value, c.actual.width, c.actual.height));
        } else {
            c.compare(bypass);
        }
        c.edit(b, b, 3, 0.11f * (mode + 1));
        auto options = c.options(0.0625f, 0, mode == 4 ? 4 : 1);
        c.compare(options);
    }
    // Multiple submitted edits since one draw must be described by their union.
    const auto image_revision = c.image.revision(), mask_revision = c.mask.revision();
    c.edit(a, a, 3, 1);
    c.edit(b, b, 3, 0);
    auto options = c.options();
    c.dirty_hint = DirtyHint{{0, 0, 129, 97}, image_revision};
    c.overlay_dirty_hint = DirtyHint{{0, 0, 129, 97}, mask_revision};
    c.compare(options);
    // New resource identity with an empty hint must rebuild; same-sized replacements.
    c.image = c.ctx.create_image(c.image.size());
    c.mask = c.ctx.create_mask(c.mask.size());
    options = c.options();
    c.dirty_hint = DirtyHint{{}, c.image.revision()};
    c.overlay_dirty_hint = DirtyHint{{}, c.mask.revision()};
    c.compare(options);
    // Logical dimensions change while storage capacity stays fixed.
    for (ImageSize size : {ImageSize{97, 129}, {127, 93}}) {
        c.image.set_size(size); c.mask.set_size(size);
        c.reference.set_size(size); c.reference_mask.set_size(size);
        c.edit({0, 0, int(size.width), int(size.height)}, {0, 0, int(size.width), int(size.height)});
        options = c.options();
        c.dirty_hint = DirtyHint{{}, c.image.revision()};
        c.overlay_dirty_hint = DirtyHint{{}, c.mask.revision()};
        c.compare(options);
    }
    // Growing a reservation invalidates the replaced buffers, even with empty bounds.
    c.partial_cache.reserve(c.partial, {1024, 1024}, options);
    c.compare(options);
}

void revision_mismatch(bool skipped_views) {
    for (float anisotropy : {1.0f, 4.0f, 0.25f, 0.0625f}) {
        Comparison c({129, 97});
        auto options = c.options(anisotropy == 0.0625f ? 0.5f : 0.125f, 0, anisotropy);
        c.compare(options);
        auto frequent = webgpu::Presenter::create(c.ctx, WGPUTextureFormat_RGBA8Unorm);
        frequent.reserve(c.image.size(), options);
        c.ctx.wait(frequent.draw(c.image, c.actual.view.value, c.actual.width, c.actual.height, options));
        // P2: each frame's same hints reach both presenters, but one skips odd frames.
        for (int frame = 0; skipped_views && frame < 8; ++frame) {
            const Rect region{frame * 13, frame * 9, 21, 19};
            c.edit(region, region, 1 + frame % 3, 0.2f * frame);
            c.ctx.wait(frequent.draw(c.image, c.actual.view.value, c.actual.width, c.actual.height,
                                    options, c.dirty_hint, c.overlay_dirty_hint));
            if (frame % 2) c.compare(options);
        }
        // P3: capacity rejection after the caller clears its accumulated region.
        c.edit({0, 0, 51, 41}, {0, 0, 51, 41}, 3, 1.7f);
        auto unreserved = c.options(0.5f, 0, 0.25f);
        test::error(ErrorCode::capacity, "presenter.draw", "options", [&] {
            (void)c.partial.draw(c.image, c.actual.view.value, c.actual.width, c.actual.height,
                                 unreserved, c.dirty_hint, c.overlay_dirty_hint);
        });
        c.edit({90, 70, 31, 23}, {90, 70, 31, 23}, 3, 0.1f);
        c.compare(options); // The new hint cannot repair the skipped earlier revision.
        // Older and future revision hints also rebuild, including empty bounds.
        for (auto revision : {std::uint64_t{0}, std::numeric_limits<std::uint64_t>::max()}) {
            c.edit({0, 0, 61, 51}, {0, 0, 61, 51}, 3, revision ? 1.3f : 0.3f);
            c.dirty_hint = c.overlay_dirty_hint = DirtyHint{{}, revision};
            c.compare(options);
        }
    }
}

void size_bounce() {
    for (float anisotropy : {1.0f, 4.0f, 0.25f, 0.0625f}) {
        for (int changed : {1, 2, 3}) {
            Comparison c({129, 97});
            auto options = c.options(anisotropy == 0.0625f ? 0.5f : 0.125f, 0, anisotropy);
            c.compare(options);
            // P4: the revision matches the cache, but edits happened in another row layout.
            const auto image_revision = c.image.revision(), mask_revision = c.mask.revision();
            if (changed & 1) c.image.set_size({97, 129});
            if (changed & 2) c.mask.set_size({97, 129});
            const Rect region{0, 40, 97, 8};
            c.edit(region, region, changed, 1.8f);
            if (changed & 1) c.image.set_size({129, 97});
            if (changed & 2) c.mask.set_size({129, 97});
            c.dirty_hint = DirtyHint{region, image_revision};
            c.overlay_dirty_hint = DirtyHint{region, mask_revision};
            c.compare(options);
        }
    }
}

void every_level() {
    for (ImageSize size : {ImageSize{257, 193}, {256, 128}, {1, 129}, {131, 1}, {1, 1}}) {
        Comparison c(size);
        // ceil(log2(max dimension)), independently from the implementation's recurrence.
        const int count = int(std::ceil(std::log2(double(std::max(size.width, size.height)))));
        for (int level = 0; level <= count; ++level) {
            const float zoom = std::ldexp(1.0f, -(level + 1));
            auto options = c.options();
            options.view = Affine::translate(88.5f, 72.5f) * Affine::scale(zoom) *
                           Affine::translate(-float(size.width) / 2, -float(size.height) / 2);
            c.compare(options);
            const Rect region{int(size.width) - 1, int(size.height) - 1, 1, 1};
            c.edit(region, region, 3, 0.1f * (level + 1));
            c.compare(options); // Includes the last 1x1 level, even on one-pixel axes.
        }
    }
}

void invalid_regions() {
    Comparison c({129, 97});
    c.compare(c.options());
    const Rect region{0, 0, 40, 40};
    c.edit(region, region);
    for (bool overlay : {false, true}) {
        auto options = c.options();
        c.dirty_hint = DirtyHint{region, c.image.revision() - 1};
        c.overlay_dirty_hint = DirtyHint{region, c.mask.revision() - 1};
        (overlay ? c.overlay_dirty_hint : c.dirty_hint) = DirtyHint{{0, 0, -1, 5}, 0};
        test::error(ErrorCode::invalid_argument, "presenter.draw",
                    overlay ? "overlay_dirty_hint.region" : "dirty_hint.region", [&] {
            (void)c.partial.draw(c.image, c.actual.view.value, c.actual.width, c.actual.height, options,
                                  c.dirty_hint, c.overlay_dirty_hint);
        });
    }
    auto options = c.options();
    c.dirty_hint = DirtyHint{region, c.image.revision() - 1};
    c.overlay_dirty_hint = DirtyHint{region, c.mask.revision() - 1};
    c.compare(options); // Failed validation did not consume the edits.
    c.dirty_hint = DirtyHint{{0, 0, -1, -1}, 0};
    auto display = webgpu::Display::create(c.ctx, 176, 144);
    display.reserve(c.image.size(), options);
    test::error(ErrorCode::invalid_argument, "display.draw", "dirty_hint.region", [&] {
        (void)display.draw(c.image, options, c.dirty_hint, c.overlay_dirty_hint);
    });
    c.dirty_hint = DirtyHint{region, c.image.revision()};
    c.ctx.wait(display.draw(c.image, options, c.dirty_hint, c.overlay_dirty_hint));
    c.edit(region, region);
    c.ctx.wait(display.draw(c.image, options, c.dirty_hint, c.overlay_dirty_hint));
}

void queued_edits() {
    Comparison c({257, 193});
    auto options = c.options();
    c.compare(options);
    for (int i = 0; i < 12; ++i) {
        const Rect region{13 * i, 9 * i, 17, 13};
        c.edit(region, region, 3, float(i) / 10);
        (void)c.partial.draw(c.image, c.actual.view.value, c.actual.width, c.actual.height, options,
                             c.dirty_hint, c.overlay_dirty_hint);
    }
    c.dirty_hint = DirtyHint{{}, c.image.revision()};
    c.overlay_dirty_hint = DirtyHint{{}, c.mask.revision()};
    c.compare(options);
}

void independent_mean() {
    // Brute-force original pixels, with no pyramid recurrence in the reference.
    auto ctx = Context::create();
    auto image = ctx.create_image({65, 65});
    auto mask = ctx.create_mask(image.size());
    auto presenter = webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA8Unorm);
    auto gpu = webgpu::native_context(ctx);
    Target target(gpu, 1, 1);
    std::vector<Color> pixels(65 * 65, {0, 0, 0, 0});
    std::vector<float> coverage(65 * 65, 0);
    webgpu::ViewportOptions options{.view = Affine::scale(1.0f / 65), .checker_size = 0,
        .checker_first = {0, 0, 0, 1}, .overlay = &mask,
        .overlay_color = {0, 0.5f, 0, 0.5f}, .overlay_selected = true};
    presenter.reserve(image.size(), options);
    ctx.wait(presenter.draw(image, target.view.value, 1, 1, options));
    std::mt19937 rng(97);
    for (int step = 0; step < 24; ++step) {
        Rect rect{int(rng() % 65), int(rng() % 65), 1 + int(rng() % 65), 1 + int(rng() % 65)};
        const float alpha = float(step % 5) / 4;
        const Color color{1.2f * alpha, -0.1f * alpha, 0.7f * alpha, alpha};
        const float mask_value = (step % 3) * 127 / 255.0f;
        const auto image_revision = image.revision(), mask_revision = mask.revision();
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.fill(image, {.color = color, .region = rect});
            cmd.fill(mask, {.coverage = mask_value, .region = rect});
        });
        for (int y = rect.y; y < std::min(rect.y + rect.height, 65); ++y)
            for (int x = rect.x; x < std::min(rect.x + rect.width, 65); ++x) {
                pixels[y * 65 + x] = color;
                coverage[y * 65 + x] = mask_value;
            }
        const float zoom = std::array{1.0f / 65, 1.0f / 128, 1.0f / 256}[step % 3];
        options.view = Affine::translate(0.5f, 0.5f) * Affine::scale(zoom) *
                       Affine::translate(-32.5f, -32.5f);
        ctx.wait(presenter.draw(image, target.view.value, 1, 1, options,
                               DirtyHint{rect, image_revision}, DirtyHint{rect, mask_revision}));
        std::array<double, 3> mean{};
        double selected = 0;
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            mean[0] += pixels[i].r / double(pixels.size());
            mean[1] += pixels[i].g / double(pixels.size());
            mean[2] += pixels[i].b / double(pixels.size());
            selected += coverage[i] / double(pixels.size());
        }
        for (auto& value : mean) value *= 1 - selected * 0.5;
        mean[1] += selected * 0.5;
        const auto actual = target.read(gpu);
        for (int channel = 0; channel < 3; ++channel)
            test::check(std::abs(int(actual[channel]) - int(test::channel(mean[channel]))) <= 1,
                        "dirty pyramid disagrees with brute-force original-pixel mean");
        test::check(actual[3] == 255, "opaque checker alpha changed");
    }
}

#ifdef WGPUPIXEL_TEST_DISPATCH_COUNTS
void failed_partial_generation() {
    for (float anisotropy : {1.0f, 4.0f, 0.25f}) {
        Comparison c({129, 97});
        auto options = c.options(0.125f, 0, anisotropy);
        c.compare(options);
        const Rect a{0, 0, 41, 33}, b{90, 70, 31, 23};
        c.edit(a, a, 3, 1);
        skip_draw = hold_callback = true;
        const auto failed = c.partial.draw(c.image, c.actual.view.value, c.actual.width, c.actual.height, options,
                                  c.dirty_hint, c.overlay_dirty_hint);
        // Queue a second partial generation BEFORE the older failure is known.
        c.edit(b, b, 3, 0.1f);
        const auto newer = c.partial.draw(c.image, c.actual.view.value, c.actual.width, c.actual.height, options,
                                  c.dirty_hint, c.overlay_dirty_hint);
        held_callback.callback(WGPUQueueWorkDoneStatus_Error, {nullptr, 0},
                               held_callback.userdata1, held_callback.userdata2);
        test::error(ErrorCode::execution_failed, "wait", "", [&] { c.ctx.wait(failed); });
        c.ctx.wait(newer);
        c.dirty_hint = DirtyHint{{}, c.image.revision()};
        c.overlay_dirty_hint = DirtyHint{{}, c.mask.revision()};
        c.compare(options); // Must discard every dependent generation, then rebuild.
    }
}

void bounded_work() {
    Comparison c({1025, 769});
    auto options = c.options();
    c.partial_cache.reserve(c.partial, c.image.size(), options);
    auto draw_groups = [&] {
        dispatched_groups = 0;
        c.ctx.wait(c.partial.draw(c.image, c.actual.view.value, c.actual.width, c.actual.height, options,
                                  c.dirty_hint, c.overlay_dirty_hint));
        return dispatched_groups;
    };
    const auto full = draw_groups();
    test::check(full > 1000, "full rebuild did not dispatch expected image and mask work");
    const auto reserved = c.ctx.memory().presentation;
    const Rect region{511, 383, 3, 3};
    c.edit(region, region);
    const auto partial = draw_groups();
    test::check(partial > 0 && partial * 100 < full, "tiny dirty region still dispatches full pyramid");
    test::check(c.ctx.memory().presentation == reserved, "partial draw grew cache storage");
    test::check(draw_groups() == 0, "unchanged revision dispatched reduction work");
    // An identical whole-image copy changes the revision without changing any pixels.
    const auto revision = c.image.revision();
    c.ctx.run_and_wait([&](Commands& cmd) { cmd.copy(c.image, c.reference); cmd.copy(c.reference, c.image); });
    c.dirty_hint = DirtyHint{{}, revision};
    test::check(draw_groups() == 0, "empty dirty region dispatched reduction work");
    c.compare(options);
}
#endif
} // namespace

int main() {
    test::run("dirty viewport random edits equal full rebuilds exactly", random_edits);
    test::run("dirty viewport cache transitions and fallbacks", cache_transitions);
    test::run("dirty viewport skipped views reject mismatched revisions", [] { revision_mismatch(true); });
    test::run("dirty viewport capacity failure rejects subsequent per-frame hints", [] { revision_mismatch(false); });
    test::run("dirty viewport size changes between draws", size_bounce);
    test::run("dirty viewport samples every pyramid level", every_level);
    test::run("dirty viewport region validation and Display forwarding", invalid_regions);
    test::run("dirty viewport queued edits need no intervening waits", queued_edits);
    test::run("dirty viewport independent full-image mean", independent_mean);
#ifdef WGPUPIXEL_TEST_DISPATCH_COUNTS
    test::run("dirty viewport dispatch work is region bounded", bounded_work);
    test::run("dirty viewport late failure invalidates dependent partial generations", failed_partial_generation);
#endif
    return test::failures ? 1 : 0;
}
