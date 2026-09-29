#include <wgpupixel.h>
#include <chrono>
#include <iostream>
#include <string>
using namespace wgpupixel;
int main(int argc, char** argv) {
    if (argc < 4) {
        return 2;
    }
    const std::string effect = argv[1];
    const ImageSize size{std::stoi(argv[2]), std::stoi(argv[3])};
    const int radius = argc > 4 ? std::stoi(argv[4]) : 100;
    auto ctx = Context::create();
    auto source = ctx.create_image(size), destination = ctx.create_image(size);
    ctx.run_and_wait([&](Commands& cmd) {
        cmd.noise(source, {.seed = 42, .monochrome = false});
        cmd.copy(source, destination);
    });
    WorkspacePlan plan;
    if (effect == "median") {
        plan = median_requirements(size, {.radius = radius}).workspace;
    }
    if (effect == "surface") {
        plan = surface_blur_requirements(size, {.radius = radius, .threshold = 100}).workspace;
    }
    if (effect == "motion") {
        plan = motion_blur_requirements(size, {.angle = 37, .distance = float(radius)}).workspace;
    }
    if (effect == "spin") {
        plan =
            radial_blur_requirements(size, {.amount = float(radius), .mode = RadialBlurMode::spin})
                .workspace;
    }
    if (effect == "zoom") {
        plan =
            radial_blur_requirements(size, {.amount = float(radius), .mode = RadialBlurMode::zoom})
                .workspace;
    }
    if (effect == "round") {
        plan = maximum_requirements(size, {.radius = radius, .shape = MorphologyShape::round})
                   .workspace;
    }
    if (effect == "square") {
        plan = maximum_requirements(size, {.radius = radius}).workspace;
    }
    if (effect == "gaussian") {
        plan = gaussian_blur_requirements(size, {.radius = radius, .sigma = float(radius) / 3})
                   .workspace;
    }
    if (effect == "unsharp") {
        plan = unsharp_mask_requirements(size, {.radius = float(radius)}).workspace;
    }
    if (effect == "highpass") {
        plan = high_pass_requirements(size, {.radius = float(radius)}).workspace;
    }
    if (effect == "box") {
        plan = box_blur_requirements(size, {.radius = radius}).workspace;
    }
    auto workspace = ctx.create_workspace(plan);
    auto cmd = ctx.create_commands();
    auto record = [&] {
        if (effect == "median") {
            cmd.median(source, destination, {.radius = radius, .workspace = workspace});
        } else if (effect == "surface") {
            cmd.surface_blur(source, destination,
                             {.radius = radius, .threshold = 100, .workspace = workspace});
        } else if (effect == "motion") {
            cmd.motion_blur(source, destination,
                            {.angle = 37, .distance = float(radius), .workspace = workspace});
        } else if (effect == "spin" || effect == "zoom") {
            cmd.radial_blur(source, destination,
                            {.amount = float(radius),
                             .mode = effect == "spin" ? RadialBlurMode::spin : RadialBlurMode::zoom,
                             .workspace = workspace});
        } else if (effect == "round" || effect == "square") {
            cmd.maximum(
                source, destination,
                {.radius = radius,
                 .shape = effect == "round" ? MorphologyShape::round : MorphologyShape::square,
                 .workspace = workspace});
        } else if (effect == "gaussian") {
            cmd.gaussian_blur(
                destination,
                {.radius = radius, .sigma = float(radius) / 3, .workspace = workspace});
        } else if (effect == "unsharp") {
            cmd.unsharp_mask(source, destination,
                             {.radius = float(radius), .workspace = workspace});
        } else if (effect == "highpass") {
            cmd.high_pass(source, destination, {.radius = float(radius), .workspace = workspace});
        } else if (effect == "box") {
            cmd.box_blur(source, destination, {.radius = radius, .workspace = workspace});
        } else {
            throw std::runtime_error("unknown filter");
        }
    };
    std::cout << effect << " " << size.width << "x" << size.height << " " << radius << std::endl;
    for (int i = 0; i < 2; ++i) {
        const auto start = std::chrono::steady_clock::now();
        record();
        const auto recorded = std::chrono::steady_clock::now();
        ctx.submit_and_wait(cmd);
        std::cout << "record "
                  << std::chrono::duration<double, std::milli>(recorded - start).count() << " ms; ";
        std::cout << (i ? "measured " : "warmup ")
                  << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                               start)
                         .count()
                  << " ms" << std::endl;
    }
}
