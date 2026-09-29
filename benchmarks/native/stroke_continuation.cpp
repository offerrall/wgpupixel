// Standalone resident 24 MP probe; build/run instructions in stroke_continuation.md.
#include <wgpupixel.h>
#include <chrono>
#include <iostream>
#include <string_view>
#include <vector>

using namespace wgpupixel;
using Clock = std::chrono::steady_clock;

int main(int argc, char** argv) {
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
                // Previous continuation algorithm: full capture on the first call,
                // full restore thereafter, followed by bounded cumulative replay.
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
        std::cout << (full_copy ? "full-copy" : "state") << " gesture " << gesture
                  << ": " << ms / 100 << " ms/call\n";
    }
    std::cout << "6000x4000, " << bounds.width << 'x' << bounds.height
              << " final bounds, 300 measured frames: " << elapsed / 300 << " ms/call\n";
}
