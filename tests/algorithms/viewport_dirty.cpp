#include "../viewport_target.h"
#include <limits>
#include <random>

using namespace wgpupixel;
using test::Target;

#ifdef WGPUPIXEL_TEST_DISPATCH_COUNTS
namespace {
std::uint64_t dispatched_groups = 0;
bool skip_draw = false, hold_callback = false;
WGPUQueueWorkDoneCallbackInfo held_callback = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
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
struct Comparison {
    Context ctx = Context::create();
    webgpu::NativeContext gpu = webgpu::native_context(ctx);
    Image image, reference;
    Mask mask, reference_mask;
    webgpu::Presenter partial = webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA8Unorm);
    webgpu::Presenter full = webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA8Unorm);
    Target actual{gpu, 176, 144}, expected{gpu, 176, 144};

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
        partial.reserve(image.size(), options);
        // Full copies advance the independent oracle's revisions on every comparison.
        auto cmd = ctx.create_commands();
        cmd.copy(image, reference);
        cmd.copy(mask, reference_mask);
        const auto copied = ctx.submit(cmd);
        const auto drawn = partial.draw(image, actual.view.value, actual.width, actual.height, options);
        if (options.overlay) options.overlay = &reference_mask;
        options.dirty_region.reset();
        options.overlay_dirty_region.reset();
        full.reserve(reference.size(), options);
        ctx.wait(full.draw(reference, expected.view.value, expected.width, expected.height, options));
        ctx.wait(drawn);
        ctx.wait(copied);
        test::check(actual.read(gpu) == expected.read(gpu), "dirty draw differs from full rebuild");
    }

    void edit(Rect image_region, Rect mask_region, int changes = 3, float value = 0.3f) {
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
            // Repeated views exercise partial axis reductions; changed views exercise all
            // pyramid levels, rotations, both anisotropic axes, and a source-level axis.
            const float scale = std::array{0.25f, 0.125f, 0.0625f, 0.2f, 0.03125f, 0.0078125f,
                                            0.125f, 0.5f}[(step / 4) % 8];
            auto options = c.options(scale, std::array{0.0f, 23.0f, 90.0f, -37.0f}[(step / 16) % 4],
                                     std::array{1.0f, 4.0f, 0.25f, 1.0f}[(step / 4) % 4]);
            options.overlay_selected = step % 2;
            options.dirty_region = a;
            options.overlay_dirty_region = b;
            if (step % 13 == 0) options.dirty_region.reset();
            if (step % 17 == 0) options.overlay_dirty_region.reset();
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
        bypass.dirty_region = bypass.overlay_dirty_region = a;
        if (mode == 1) bypass.overlay = nullptr;
        if (mode == 2) {
            c.ctx.wait(c.partial.draw(c.image, c.actual.view.value, c.actual.width, c.actual.height));
        } else {
            c.compare(bypass);
        }
        c.edit(b, b, 3, 0.11f * (mode + 1));
        auto options = c.options(0.0625f, 0, mode == 4 ? 4 : 1);
        options.dirty_region = options.overlay_dirty_region = b;
        c.compare(options);
    }
    // Multiple submitted edits since one draw must be described by their union.
    c.edit(a, a, 3, 1);
    c.edit(b, b, 3, 0);
    auto options = c.options();
    options.dirty_region = options.overlay_dirty_region = Rect{0, 0, 129, 97};
    c.compare(options);
    // New resource identity with an empty hint must rebuild; same-sized replacements.
    c.image = c.ctx.create_image(c.image.size());
    c.mask = c.ctx.create_mask(c.mask.size());
    options = c.options();
    options.dirty_region = options.overlay_dirty_region = Rect{};
    c.compare(options);
    // Logical dimensions change while storage capacity stays fixed.
    for (ImageSize size : {ImageSize{97, 129}, {127, 93}}) {
        c.image.set_size(size); c.mask.set_size(size);
        c.reference.set_size(size); c.reference_mask.set_size(size);
        c.edit({0, 0, int(size.width), int(size.height)}, {0, 0, int(size.width), int(size.height)});
        options = c.options();
        options.dirty_region = options.overlay_dirty_region = Rect{};
        c.compare(options);
    }
    // Growing a reservation invalidates the replaced buffers, even with empty bounds.
    c.partial.reserve({1024, 1024}, options);
    c.compare(options);
}

void invalid_regions() {
    Comparison c({129, 97});
    c.compare(c.options());
    const Rect region{0, 0, 40, 40};
    c.edit(region, region);
    for (bool overlay : {false, true}) {
        auto options = c.options();
        (overlay ? options.overlay_dirty_region : options.dirty_region) = Rect{0, 0, -1, 5};
        test::error(ErrorCode::invalid_argument, "presenter.draw",
                    overlay ? "overlay_dirty_region" : "dirty_region", [&] {
            (void)c.partial.draw(c.image, c.actual.view.value, c.actual.width, c.actual.height, options);
        });
    }
    auto options = c.options();
    options.dirty_region = options.overlay_dirty_region = region;
    c.compare(options); // Failed validation did not consume the edits.
    const auto before = webgpu::viewport_requirements(c.image.size(), options);
    options.dirty_region = Rect{0, 0, -1, -1};
    test::check(webgpu::viewport_requirements(c.image.size(), options).total == before.total,
                "dirty hints changed pure cache requirements");
    auto display = webgpu::Display::create(c.ctx, 176, 144);
    display.reserve(c.image.size(), options);
    test::error(ErrorCode::invalid_argument, "display.draw", "dirty_region", [&] {
        (void)display.draw(c.image, options);
    });
    options.dirty_region = region;
    c.ctx.wait(display.draw(c.image, options));
    c.edit(region, region);
    c.ctx.wait(display.draw(c.image, options));
}

void queued_edits() {
    Comparison c({257, 193});
    auto options = c.options();
    c.compare(options);
    for (int i = 0; i < 12; ++i) {
        const Rect region{13 * i, 9 * i, 17, 13};
        c.edit(region, region, 3, float(i) / 10);
        options.dirty_region = options.overlay_dirty_region = region;
        (void)c.partial.draw(c.image, c.actual.view.value, c.actual.width, c.actual.height, options);
    }
    options.dirty_region = options.overlay_dirty_region = Rect{};
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
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.fill(image, {.color = color, .region = rect});
            cmd.fill(mask, {.coverage = mask_value, .region = rect});
        });
        for (int y = rect.y; y < std::min(rect.y + rect.height, 65); ++y)
            for (int x = rect.x; x < std::min(rect.x + rect.width, 65); ++x) {
                pixels[y * 65 + x] = color;
                coverage[y * 65 + x] = mask_value;
            }
        options.dirty_region = options.overlay_dirty_region = rect;
        ctx.wait(presenter.draw(image, target.view.value, 1, 1, options));
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
        options.dirty_region = options.overlay_dirty_region = a;
        skip_draw = hold_callback = true;
        const auto failed = c.partial.draw(c.image, c.actual.view.value, c.actual.width, c.actual.height, options);
        // Queue a second partial generation BEFORE the older failure is known.
        c.edit(b, b, 3, 0.1f);
        options.dirty_region = options.overlay_dirty_region = b;
        const auto newer = c.partial.draw(c.image, c.actual.view.value, c.actual.width, c.actual.height, options);
        held_callback.callback(WGPUQueueWorkDoneStatus_Error, {nullptr, 0},
                               held_callback.userdata1, held_callback.userdata2);
        test::error(ErrorCode::execution_failed, "wait", "", [&] { c.ctx.wait(failed); });
        c.ctx.wait(newer);
        options.dirty_region = options.overlay_dirty_region = Rect{};
        c.compare(options); // Must discard every dependent generation, then rebuild.
    }
}

void bounded_work() {
    Comparison c({1025, 769});
    auto options = c.options();
    c.partial.reserve(c.image.size(), options);
    auto draw_groups = [&] {
        dispatched_groups = 0;
        c.ctx.wait(c.partial.draw(c.image, c.actual.view.value, c.actual.width, c.actual.height, options));
        return dispatched_groups;
    };
    const auto full = draw_groups();
    test::check(full > 1000, "full rebuild did not dispatch expected image and mask work");
    const auto reserved = c.ctx.memory().presentation;
    const Rect region{511, 383, 3, 3};
    c.edit(region, region);
    options.dirty_region = options.overlay_dirty_region = region;
    const auto partial = draw_groups();
    test::check(partial > 0 && partial * 100 < full, "tiny dirty region still dispatches full pyramid");
    test::check(c.ctx.memory().presentation == reserved, "partial draw grew cache storage");
    test::check(draw_groups() == 0, "unchanged revision dispatched reduction work");
    // An identical whole-image copy changes the revision without changing any pixels.
    c.ctx.run_and_wait([&](Commands& cmd) { cmd.copy(c.image, c.reference); cmd.copy(c.reference, c.image); });
    options.dirty_region = Rect{};
    test::check(draw_groups() == 0, "empty dirty region dispatched reduction work");
    c.compare(options);
}
#endif
} // namespace

int main() {
    test::run("dirty viewport random edits equal full rebuilds exactly", random_edits);
    test::run("dirty viewport cache transitions and fallbacks", cache_transitions);
    test::run("dirty viewport region validation and Display forwarding", invalid_regions);
    test::run("dirty viewport queued edits need no intervening waits", queued_edits);
    test::run("dirty viewport independent full-image mean", independent_mean);
#ifdef WGPUPIXEL_TEST_DISPATCH_COUNTS
    test::run("dirty viewport dispatch work is region bounded", bounded_work);
    test::run("dirty viewport late failure invalidates dependent partial generations", failed_partial_generation);
#endif
    return test::failures ? 1 : 0;
}
