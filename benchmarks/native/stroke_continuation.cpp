// Standalone resident 24 MP probe; build/run instructions in stroke_continuation.md.
#include <wgpupixel.h>
#include <array>
#include <chrono>
#include <iostream>
#include <string_view>
#include <vector>

using namespace wgpupixel;
using Clock = std::chrono::steady_clock;

int full_mask_probe() {
    auto ctx = Context::create();
    ctx.prepare();
    auto mask = ctx.create_mask({6000, 4000});
    auto state = ctx.create_brush_stroke_state(mask);
    auto cmd = ctx.create_commands();
    const std::array samples{StrokeSample{{3000, 2000}}, StrokeSample{{3001, 2000}},
                            StrokeSample{{3000, 2001}}, StrokeSample{{3001, 2001}}};
    const Brush brush{.diameter = 8000, .spacing = 0};
    double elapsed = 0;
    for (int gesture = 0; gesture < 11; ++gesture) {
        state.reset();
        cmd.fill(mask, {.coverage = .25f});
        ctx.submit_and_wait(cmd);
        const auto start = Clock::now();
        for (const auto& sample : samples) {
            cmd.brush_stroke(mask, state,
                {.samples = std::span(&sample, 1), .brush = brush, .coverage = .8f, .opacity = .5f});
            ctx.submit_and_wait(cmd);
        }
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        if (gesture > 0) elapsed += ms;
        std::cout << "full-mask gesture " << gesture << ": " << ms / samples.size()
                  << " ms/call\n";
    }
    const auto bounds = *state.snapshot_bounds();
    std::cout << "6000x4000 A8, " << bounds.width << 'x' << bounds.height
              << " final bounds, 40 measured frames: " << elapsed / 40 << " ms/call\n";
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "full-mask") return full_mask_probe();
    const bool full_copy = argc > 1 && std::string_view(argv[1]) == "full-copy";
    auto ctx = Context::create();
    ctx.prepare();
    auto image = ctx.create_image({6000, 4000});
    BrushStrokeState state;
    Image snapshot;
    if (full_copy) snapshot = ctx.create_image(image.size());
    else state = ctx.create_brush_stroke_state(image);
    auto cmd = ctx.create_commands();
    std::vector<StrokeSample> samples;
    for (int i = 0; i < 200; ++i)
        samples.push_back({{float(100 + i * 10), float(100 + i * 5)}});
    const Brush brush{.diameter = 40};
    const auto bounds = *stroke_bounds(samples, brush);
    double elapsed = 0;
    for (int gesture = 0; gesture < 4; ++gesture) {
        state.reset();
        cmd.fill(image, {.color = {0, .2f, .4f, .8f}});
        ctx.submit_and_wait(cmd);
        const auto start = Clock::now();
        for (std::size_t n = 2; n <= samples.size(); n += 2) {
            if (full_copy) {
                // Emulate full capture/restore via public copy + one-shot replay;
                // this does not invoke main's continue_stroke implementation.
                if (n == 2) cmd.copy(image, snapshot);
                else cmd.copy(snapshot, image);
                cmd.brush_stroke(image, {.samples = std::span(samples).first(n), .brush = brush,
                                        .color = {1, 0, 0, 1}, .opacity = .5f});
            } else {
                cmd.brush_stroke(image, state,
                    {.samples = std::span(samples).subspan(n - 2, 2), .brush = brush,
                     .color = {1, 0, 0, 1}, .opacity = .5f});
            }
            ctx.submit_and_wait(cmd);
        }
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        if (gesture > 0) elapsed += ms;
        std::cout << (full_copy ? "emulated full-copy" : "state") << " gesture " << gesture
                  << ": " << ms / 100 << " ms/call\n";
    }
    std::cout << "6000x4000, " << bounds.width << 'x' << bounds.height
              << " final bounds, 300 measured frames: " << elapsed / 300 << " ms/call\n";
}
