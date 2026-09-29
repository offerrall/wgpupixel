// Standalone: link with wgpupixel and wgpu_native; see viewport_dirty.md.
#include <wgpupixel_webgpu.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace wgpupixel;
using Clock = std::chrono::steady_clock;

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return (values[(values.size() - 1) / 2] + values[values.size() / 2]) / 2;
}

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
    std::cout << "6000x4000, 1600x1000 target, 20% Fit with margin, 512x512 edit\n"
              << "5 runs per case, each 8 warm-up + 40 measured frames\n"
              << "cache bytes: " << webgpu::viewport_requirements(image.size(), options).total << '\n'
              << std::fixed << std::setprecision(3);
    std::array<std::vector<double>, 6> draw_medians, frame_medians;
    const std::array names{"image full", "image dirty", "image no-edit",
                           "image+mask full", "image+mask dirty", "image+mask no-edit"};
    for (int run = 0; run < 5; ++run) {
        // Rotate case order to avoid tying any mode to a particular warm-up/load period.
        for (int index = 0; index < 6; ++index) {
            const int which = (index + run) % 6;
            const bool overlay = which >= 3, dirty = which % 3 == 1, edit = which % 3 != 2;
#ifndef WGPUPIXEL_VIEWPORT_DIRTY
            if (dirty) continue;
#endif
            options.overlay = overlay ? &mask : nullptr;
            std::vector<double> draw_times, frame_times;
            for (int i = 0; i < 48; ++i) {
                const Rect region{(i * 97) % (6000 - 512), (i * 71) % (4000 - 512), 512, 512};
#ifdef WGPUPIXEL_VIEWPORT_DIRTY
                const auto image_revision = image.revision(), mask_revision = mask.revision();
#endif
                const auto begin = Clock::now();
                if (edit) {
                    commands.fill(image, {.color = {0.1f + 0.01f * i, 0.3f, 0.1f, 0.8f},
                                          .region = region});
                    if (overlay) commands.fill(mask, {.coverage = float(i % 5) / 4, .region = region});
                    // Include edits in total latency; isolate draw by waiting before timing it.
                    ctx.wait(ctx.submit(commands));
                }
                const auto draw_begin = Clock::now();
#ifdef WGPUPIXEL_VIEWPORT_DIRTY
                const auto image_hint = dirty ? std::optional{webgpu::DirtyHint{region, image_revision}}
                                              : std::nullopt;
                const auto mask_hint = dirty ? std::optional{webgpu::DirtyHint{region, mask_revision}}
                                             : std::nullopt;
                ctx.wait(display.draw(image, options, image_hint, mask_hint));
#else
                ctx.wait(display.draw(image, options));
#endif
                const auto end = Clock::now();
                if (i >= 8) {
                    draw_times.push_back(std::chrono::duration<double, std::milli>(end - draw_begin).count());
                    frame_times.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
                }
            }
            draw_medians[which].push_back(median(draw_times));
            frame_medians[which].push_back(median(frame_times));
            std::cout << "run " << run + 1 << ' ' << names[which]
                      << " draw median " << median(draw_times)
                      << " edit+draw median " << median(frame_times) << '\n' << std::flush;
        }
    }
    for (int which = 0; which < 6; ++which) {
        const auto& draws = draw_medians[which];
        if (draws.empty()) continue;
        std::cout << names[which] << " draw median-of-medians " << median(draws)
                  << " run range " << *std::min_element(draws.begin(), draws.end()) << '-'
                  << *std::max_element(draws.begin(), draws.end())
                  << " edit+draw median-of-medians " << median(frame_medians[which]) << '\n';
    }
}
