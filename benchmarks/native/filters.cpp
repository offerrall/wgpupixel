#include <wgpupixel.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>
using namespace wgpupixel;
int main() {
    auto ctx = Context::create();
    auto image = ctx.create_image({1024, 1024});
    ctx.run_and_wait([&](Commands& cmd) { cmd.noise(image, {.seed = 42}); });
    for (int radius : {8, 32}) {
        auto workspace = ctx.create_workspace(
            gaussian_blur_requirements(image.size(), {.radius = radius, .sigma = float(radius) / 3})
                .workspace);
        std::vector<double> times;
        for (int trial = 0; trial < 9; ++trial) {
            auto cmd = ctx.create_commands(20);
            const auto begin = std::chrono::steady_clock::now();
            for (int i = 0; i < 10; ++i) {
                cmd.gaussian_blur(
                    image, {.radius = radius, .sigma = float(radius) / 3, .workspace = workspace});
            }
            ctx.submit_and_wait(cmd);
            const auto end = std::chrono::steady_clock::now();
            if (trial > 1) {
                times.push_back(std::chrono::duration<double, std::milli>(end - begin).count() /
                                10);
            }
        }
        std::ranges::sort(times);
        std::cout << radius << " median ms/blur " << times[times.size() / 2] << '\n';
    }
}
