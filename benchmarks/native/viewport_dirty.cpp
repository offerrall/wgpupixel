// Standalone: link with wgpupixel and wgpu_native; see viewport_dirty.md.
#include <wgpupixel_webgpu.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace wgpupixel;
using Clock = std::chrono::steady_clock;

int main() {
    auto ctx = Context::create();
    auto image = ctx.create_image({6000, 4000});
    auto mask = ctx.create_mask(image.size());
    auto commands = ctx.create_commands();
    commands.fill(image, {.color = {0.2f, 0.3f, 0.1f, 0.8f}});
    commands.fill(mask, {.coverage = 0.5f});
    ctx.submit_and_wait(commands);
    auto display = webgpu::Display::create(ctx, 1600, 1000);
    const Affine fit = Affine::translate(200, 100) * Affine::scale(0.2f);
    webgpu::ViewportOptions options{.view = fit, .overlay = &mask};
    display.reserve(image.size(), options);
    std::cout << "6000x4000, 1600x1000 target, 20% Fit with margin, 512x512 edit\n";
    std::cout << "cache bytes: " << webgpu::viewport_requirements(image.size(), options).total << '\n';
    for (bool overlay : {false, true}) {
        options.overlay = overlay ? &mask : nullptr;
        for (bool dirty : {false, true}) {
#ifndef WGPUPIXEL_VIEWPORT_DIRTY
            if (dirty) continue;
#endif
            std::vector<double> draw_times, frame_times;
            for (int i = 0; i < 48; ++i) {
                const Rect region{(i * 97) % (6000 - 512), (i * 71) % (4000 - 512), 512, 512};
#ifdef WGPUPIXEL_VIEWPORT_DIRTY
                options.dirty_region = dirty ? std::optional{region} : std::nullopt;
                options.overlay_dirty_region = options.dirty_region;
#endif
                const auto begin = Clock::now();
                commands.fill(image, {.color = {0.1f + 0.01f * i, 0.3f, 0.1f, 0.8f},
                                      .region = region});
                if (overlay) commands.fill(mask, {.coverage = float(i % 5) / 4, .region = region});
                const auto edited = ctx.submit(commands);
                // Include edits in total latency; isolate draw by waiting before timing it.
                ctx.wait(edited);
                const auto draw_begin = Clock::now();
                ctx.wait(display.draw(image, options));
                const auto end = Clock::now();
                if (i >= 8) {
                    draw_times.push_back(std::chrono::duration<double, std::milli>(end - draw_begin).count());
                    frame_times.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
                }
            }
            std::sort(draw_times.begin(), draw_times.end());
            std::sort(frame_times.begin(), frame_times.end());
            std::cout << (overlay ? "image+mask" : "image     ") << (dirty ? " dirty" : " full ")
                      << std::fixed << std::setprecision(3)
                      << " draw ms min/median/p90 " << draw_times.front() << '/' << draw_times[20]
                      << '/' << draw_times[36] << " edit+draw median " << frame_times[20] << '\n';
        }
    }
}
