#include <wgpupixel.h>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
using namespace wgpupixel;
int main(int argc, char** argv) {
    const int side = argc > 1 ? std::stoi(argv[1]) : 1024;
    if (side < 1 || std::int64_t(side) * side > 24000000) {
        return 2;
    }
    auto ctx = Context::create();
    auto source = ctx.create_image({side, side}), destination = ctx.create_image({side, side});
    ctx.run_and_wait([&](Commands& cmd) { cmd.fill(source, {.color = {.125f, .25f, .5f, 1}}); });
    const std::array names{"median r1",          "surface r3",   "round maximum r3",
                           "square maximum r16", "motion 32px",  "radial 31px",
                           "box r1024",          "mosaic cell2", "mosaic cell1024",
                           "gaussian r32",       "unsharp r16"};
    auto readback = ctx.create_readback_buffer(
        destination, {.format = TransferFormat::rgba32_float, .capacity_pixels = 1});
    WorkspacePlan plan;
    plan.merge(maximum_requirements(source.size(), {.radius = 16}).workspace);
    plan.merge(box_blur_requirements(source.size(), {.radius = 1024}).workspace);
    plan.merge(
        gaussian_blur_requirements(source.size(), {.radius = 32, .sigma = 32.f / 3}).workspace);
    plan.merge(unsharp_mask_requirements(source.size(), {.radius = 16}).workspace);
    auto workspace = ctx.create_workspace(plan);
    for (int effect = 0; effect < int(names.size()); ++effect) {
        std::cout << side << "x" << side << " " << names[effect] << std::endl;
        auto cmd = ctx.create_commands(8);
        auto record = [&] {
            switch (effect) {
            case 0:
                cmd.median(source, destination);
                break;
            case 1:
                cmd.surface_blur(source, destination, {.radius = 3, .workspace = workspace});
                break;
            case 2:
                cmd.maximum(source, destination,
                            {.radius = 3, .shape = MorphologyShape::round, .workspace = workspace});
                break;
            case 3:
                cmd.maximum(source, destination, {.radius = 16, .workspace = workspace});
                break;
            case 4:
                cmd.motion_blur(source, destination, {.distance = 32, .workspace = workspace});
                break;
            case 5:
                cmd.radial_blur(source, destination,
                                {.amount = float(std::min(
                                     100., 31. / (std::max(1., (side - 1.) / std::sqrt(2.)) *
                                                  .017453292519943295))),
                                 .workspace = workspace});
                break;
            case 6:
                cmd.box_blur(source, destination, {.radius = 1024, .workspace = workspace});
                break;
            case 7:
                cmd.pixelate(source, destination, {.cell_size = 2});
                break;
            case 8:
                cmd.pixelate(source, destination, {.cell_size = 1024});
                break;
            case 9:
                cmd.copy(source, destination);
                cmd.gaussian_blur(destination,
                                  {.radius = 32, .sigma = 32.f / 3, .workspace = workspace});
                break;
            case 10:
                cmd.unsharp_mask(source, destination, {.radius = 16, .workspace = workspace});
                break;
            }
        };
        for (int trial = 0; trial < 2; ++trial) {
            const auto start = std::chrono::steady_clock::now();
            record();
            ctx.submit_and_wait(cmd);
            const auto ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count();
            std::cout << (trial ? "measured " : "warmup ") << ms << " ms" << std::endl;
            if (ms > 5000) {
                std::cerr << "Workload exceeded the editor latency budget\n";
                return 1;
            }
        }
        cmd.download(destination, readback, {.region = Rect{side / 2, side / 2, 1, 1}});
        ctx.submit_and_wait(cmd);
        std::array<float, 4> actual{};
        ctx.read(readback, {reinterpret_cast<std::uint8_t*>(actual.data()), sizeof(actual)});
        const std::array expected{.125f, .25f, .5f, 1.f};
        for (int c = 0; c < 4; ++c) {
            if (!std::isfinite(actual[c]) || std::abs(actual[c] - expected[c]) > 1e-4) {
                return 1;
            }
        }
    }
}
