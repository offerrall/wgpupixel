#include "../test.h"
#include <array>
#include <limits>
using namespace wgpupixel;
namespace {
using Pixel = std::array<float, 4>;
using Pixels = std::vector<Pixel>;
void put(Context& ctx, const Image& image, const Pixels& pixels) {
    auto upload = ctx.create_upload_buffer(image, {.format = TransferFormat::rgba32_float});
    ctx.write(upload, {reinterpret_cast<const std::uint8_t*>(pixels.data()),
                       pixels.size() * sizeof(Pixel)});
    ctx.run_and_wait([&](Commands& cmd) { cmd.upload(upload, image); });
    ctx.destroy(upload);
}
Pixels get(Context& ctx, const Image& image) {
    Pixels pixels(image.size().width * image.size().height);
    auto readback = ctx.create_readback_buffer(image, {.format = TransferFormat::rgba32_float});
    ctx.run_and_wait([&](Commands& cmd) { cmd.download(image, readback); });
    ctx.read(readback,
             {reinterpret_cast<std::uint8_t*>(pixels.data()), pixels.size() * sizeof(Pixel)});
    ctx.destroy(readback);
    return pixels;
}
void near(Pixel actual, Pixel expected, float tolerance = 2e-5f) {
    for (int c = 0; c < 4; ++c) {
        test::near(actual[c], expected[c], tolerance);
    }
}
float decode(float value) {
    return value <= .04045f ? value / 12.92f : std::pow((value + .055f) / 1.055f, 2.4f);
}
} // namespace
int main() {
    test::run("compensated box sums retain dark HDR tails across segments", [] {
        auto ctx = Context::create();
        constexpr int w = 259, h = 17;
        auto source = ctx.create_image({w, h}), destination = ctx.create_image({w, h}),
             scratch = ctx.create_image({w, h});
        Pixels input(w * h, Pixel{.01f, -.01f, .125f, 1});
        for (int y = 0; y < h; ++y) {
            input[y * w] = {1e6f, -1e6f, .125f, 1};
        }
        put(ctx, source, input);
        for (int r : {1, 3, 128, 1024}) {
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.box_blur(source, destination,
                             test::reserve_workspace(ctx, source, BoxBlurOptions{.radius = r},
                                                     box_blur_requirements));
            });
            const auto actual = get(ctx, destination);
            for (int x = 0; x < w; ++x) {
                // Count the edge highlight's copies analytically, including clamp extension.
                const int copies = std::max(0, r - x + 1);
                const double expected =
                    (copies * 1e6 + (2 * r + 1 - copies) * double(.01f)) / (2 * r + 1);
                for (int y = 0; y < h; ++y) {
                    const float tolerance = float(std::max(5e-8, std::abs(expected) * 3e-6));
                    test::near(actual[y * w + x][0], float(expected), tolerance);
                    test::near(actual[y * w + x][1], -float(expected), tolerance);
                }
            }
            if (r == 1) {
                test::near(actual[10][0], .01f, 5e-8f);
                test::near(actual[100][0], .01f, 5e-8f);
            }
        }
    });
    test::run("negligible noise preserves signed HDR by default", [] {
        auto ctx = Context::create();
        constexpr int w = 41, h = 19;
        auto image = ctx.create_image({w, h});
        Pixels input(w * h, Pixel{5, -2, .01f, 1});
        put(ctx, image, input);
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.add_noise(image, {.amount = .1f, .seed = 7, .region = Rect{7, 3, 27, 13}});
        });
        const auto actual = get(ctx, image);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const auto p = actual[y * w + x];
                if (x >= 7 && x < 34 && y >= 3 && y < 16) {
                    for (int c = 0; c < 3; ++c) {
                        test::check(std::abs(p[c] - input[0][c]) <= .00101f, "noise destroyed HDR");
                    }
                    test::near(p[3], 1);
                } else {
                    near(p, input[0]);
                }
            }
        }
    });
    test::run("Gaussian requirements accept large supports", [] {
        const auto repro = gaussian_blur_requirements({4000, 3000}, {.radius = 100, .sigma = 33});
        test::check(repro.destination == ImageSize{4000, 3000}, "exact reviewer Gaussian repro");
        for (auto size : {ImageSize{4000, 3000}, ImageSize{6000, 4000}, ImageSize{6000, 4001}}) {
            for (int r : {100, 200, 250, 1000, 2147483647}) {
                auto q = gaussian_blur_requirements(size, {.radius = r, .sigma = float(r) / 3});
                test::check(q.destination == size &&
                                q.workspace.bytes() <= 3 * size.width * size.height * 16,
                            "Gaussian scratch bound");
            }
        }

        for (auto mode : {RadialBlurMode::spin, RadialBlurMode::zoom}) {
            for (float amount : {2.f, 100.f}) {
                test::check(radial_blur_requirements({3000, 2000}, {.amount = amount, .mode = mode})
                                    .workspace.bytes() == 2ull * 3000 * 2000 * 16,
                            "two radial working images");
            }
        }

    });
    test::run("wide Gaussian and detail match a discrete HDR step within pyramid error", [] {
        auto ctx = Context::create();
        constexpr int w = 513, h = 37;
        auto source = ctx.create_image({w, h}), destination = ctx.create_image({w, h}),
             scratch = ctx.create_image({w, h}), blurred = ctx.create_image({w, h});
        auto mask = ctx.create_mask({w, h});
        ctx.run_and_wait([&](Commands& cmd) { cmd.fill(mask, {.coverage = 128.f / 255}); });
        Pixels input(w * h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                input[y * w + x] = {x < 256 ? -2.f : 6.f, .25f, .5f, 1};
            }
        }
        put(ctx, source, input);
        for (int operation = 0; operation < 3; ++operation) {
            const double sigma = operation == 0 ? 250. / 3 : 250.;
            const int radius = operation == 0 ? 250 : 750;
            std::vector<double> cdf(2 * radius + 2);
            for (int k = -radius; k <= radius; ++k) {
                cdf[k + radius + 1] = cdf[k + radius] + std::exp(-.5 * k * k / (sigma * sigma));
            }
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.copy(source, destination);
                if (operation == 0) {
                    cmd.gaussian_blur(
                        destination,
                        test::reserve_workspace(ctx, destination,
                                                GaussianBlurOptions{.radius = radius,
                                                                    .sigma = float(sigma),
                                                                    .mask = &mask,
                                                                    .region = Rect{17, 9, 479, 19}},
                                                gaussian_blur_requirements));
                } else if (operation == 1) {
                    cmd.unsharp_mask(
                        source, destination,
                        test::reserve_workspace(ctx, source,
                                                UnsharpMaskOptions{.radius = 250,
                                                                   .mask = &mask,
                                                                   .region = Rect{17, 9, 479, 19}},
                                                unsharp_mask_requirements));
                } else {
                    cmd.high_pass(
                        source, destination,
                        test::reserve_workspace(ctx, source,
                                                HighPassOptions{.radius = 250,
                                                                .mask = &mask,
                                                                .region = Rect{17, 9, 479, 19}},
                                                high_pass_requirements));
                }
            });
            const auto actual = get(ctx, destination);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    Pixel expected = input[y * w + x];
                    if (x >= 17 && x < 496 && y >= 9 && y < 28) {
                        const int count = std::clamp(256 - x + radius, 0, 2 * radius + 1);
                        const double mean = 6 - 8 * cdf[count] / cdf.back();
                        const double filtered = operation == 0   ? mean
                                                : operation == 1 ? 2 * expected[0] - mean
                                                                 : .5 + expected[0] - mean;
                        expected[0] += float(filtered - expected[0]) * (128.f / 255);
                        if (operation == 2) {
                            expected[1] += (.5f - expected[1]) * (128.f / 255);
                            expected[2] += (.5f - expected[2]) * (128.f / 255);
                        }
                    }
                    near(actual[y * w + x], expected, .025f);
                }
            }
        }
    });
    test::run("exact median ranks agree with independent nth_element", [] {
        auto ctx = Context::create();
        constexpr int w = 53, h = 37;
        auto source = ctx.create_image({w, h}), destination = ctx.create_image({w, h});
        auto mask = ctx.create_mask({w, h});
        Pixels input(w * h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const unsigned n = unsigned(x * 31 + y * 97 + x * y * 7);
                const float a = std::array{0.f, .5f, 1.f}[n % 3];
                input[y * w + x] = {a * (-3 + 10 * float(n % 257) / 256),
                                    a * (2 - 4 * float(n % 131) / 130), a * .25f, a};
            }
        }
        put(ctx, source, input);
        ctx.run_and_wait([&](Commands& cmd) { cmd.fill(mask, {.coverage = 128.f / 255}); });
        for (int r : {2, 10}) {
            auto cmd =
                ctx.create_commands();
            cmd.fill(destination, {.color = {-.25f, .5f, .25f, 1}});
            cmd.median(source, destination,
                       {.radius = r, .mask = &mask, .region = Rect{7, 6, 37, 23},
                        .workspace = ctx.create_workspace(median_requirements(source.size(), {.radius = r}).workspace)});
            ctx.submit_and_wait(cmd);
            const auto actual = get(ctx, destination);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    Pixel expected{-.25f, .5f, .25f, 1};
                    if (x >= 7 && x < 44 && y >= 6 && y < 29) {
                        Pixel filtered{};
                        for (int c = 0; c < 4; ++c) {
                            std::vector<float> values;
                            for (int yy = y - r; yy <= y + r; ++yy) {
                                for (int xx = x - r; xx <= x + r; ++xx) {
                                    values.push_back(input[std::clamp(yy, 0, h - 1) * w +
                                                           std::clamp(xx, 0, w - 1)][c]);
                                }
                            }
                            auto middle = values.begin() + values.size() / 2;
                            std::nth_element(values.begin(), middle, values.end());
                            filtered[c] = *middle;
                        }
                        if (filtered[3] == 0) {
                            filtered = {};
                        }
                        for (int c = 0; c < 4; ++c) {
                            expected[c] += (filtered[c] - expected[c]) * (128.f / 255);
                        }
                    }
                    near(actual[y * w + x], expected, 2e-5f);
                }
            }
        }
    });
    test::run("radius 100 and 500 median preserves an analytic edge across strips", [] {
        auto ctx = Context::create();
        constexpr int w = 2059, h = 689;
        auto source = ctx.create_image({w, h}), destination = ctx.create_image({w, h});
        Pixels input(w * h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                input[y * w + x] = x < 1030 ? Pixel{-2, .25f, 0, 1} : Pixel{7, .25f, 1, 1};
            }
        }
        put(ctx, source, input);
        for (int r : {100, 500}) {
            auto cmd =
                ctx.create_commands();
            cmd.median(source, destination, {.radius = r,
                .workspace = ctx.create_workspace(median_requirements(source.size(), {.radius = r}).workspace)});
            const auto submission = ctx.submit(cmd);
            test::error(ErrorCode::resource_busy, "destroy", "resource",
                        [&] { ctx.destroy(source); });
            ctx.wait(submission);
            const auto actual = get(ctx, destination);
            for (std::size_t i = 0; i < actual.size(); ++i) {
                near(actual[i], input[i]);
            }
        }
    });
    test::run("long motion preserves an affine HDR field", [] {
        auto ctx = Context::create();
        constexpr int w = 4099, h = 19;
        auto source = ctx.create_image({w, h}), destination = ctx.create_image({w, h});
        Pixels input(w * h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                input[y * w + x] = {-2 + 7 * float(x) / (w - 1), .25f, .5f, 1};
            }
        }
        put(ctx, source, input);
        for (float distance : {40.f, 2000.f}) {
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.copy(source, destination);
                cmd.motion_blur(
                    source, destination,
                    test::reserve_workspace(
                        ctx, source,
                        MotionBlurOptions{.distance = distance, .region = Rect{1001, 3, 2097, 13}},
                        motion_blur_requirements));
            });
            const auto actual = get(ctx, destination);
            for (std::size_t i = 0; i < actual.size(); ++i) {
                near(actual[i], input[i], 2e-5f);
            }
        }
    });
    test::run("full radial exposure obeys rotational and linear-scale integrals", [] {
        auto ctx = Context::create();
        constexpr int w = 513, h = 385;
        auto source = ctx.create_image({w, h}), destination = ctx.create_image({w, h});
        for (bool zoom : {false, true}) {
            Pixels input(w * h);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const float dx = x - 256, dy = y - 192;
                    const float v =
                        zoom ? -2 + 7 * dx / 512 : -2 + 7 * (dx * dx + dy * dy) / (192 * 192);
                    input[y * w + x] = {v, .25f, .5f, 1};
                }
            }
            put(ctx, source, input);
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.radial_blur(
                    source, destination,
                    test::reserve_workspace(ctx, source,
                                            RadialBlurOptions{.amount = 100,
                                                              .mode = zoom ? RadialBlurMode::zoom
                                                                           : RadialBlurMode::spin},
                                            radial_blur_requirements));
            });
            const auto actual = get(ctx, destination);
            for (int y = 32; y < h - 32; ++y) {
                for (int x = 96; x < w - 96; ++x) {
                    if (!zoom && std::hypot(x - 256, y - 192) > 150) {
                        continue;
                    }
                    Pixel expected = input[y * w + x];
                    if (zoom) {
                        expected[0] = -2 + (expected[0] + 2) * .5f;
                    }
                    near(actual[y * w + x], expected, .004f);
                }
            }
        }
    });
    test::run("square extrema and round radius 500 have analytic geometry", [] {
        auto ctx = Context::create();
        constexpr int w = 1101, h = 1101;
        auto source = ctx.create_image({w, h}), destination = ctx.create_image({w, h}),
             scratch = ctx.create_image({w, h});
        Pixels input(w * h, Pixel{0, 0, 0, 1});
        input[550 * w + 550] = {1, 1, 1, 1};
        put(ctx, source, input);
        for (auto shape : {MorphologyShape::square, MorphologyShape::round}) {
            auto cmd = ctx.create_commands();
            cmd.maximum(source, destination,
                        test::reserve_workspace(ctx, source,
                                                MorphologyOptions{.radius = 500, .shape = shape},
                                                maximum_requirements));
            ctx.submit_and_wait(cmd);
            const auto actual = get(ctx, destination);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const double distance = shape == MorphologyShape::square
                                                ? std::max(std::abs(x - 550), std::abs(y - 550))
                                                : std::hypot(x - 550, y - 550);
                    const double error = shape == MorphologyShape::square ? 0 : 19;
                    if (distance <= 500 - error) {
                        near(actual[y * w + x], {1, 1, 1, 1});
                    }
                    if (distance > 500 + error) {
                        near(actual[y * w + x], {0, 0, 0, 1});
                    }
                }
            }
        }
    });
    test::run("large surface blur agrees with brute-force bilateral on a noisy HDR edge", [] {
        auto ctx = Context::create();
        constexpr int w = 257, h = 129;
        auto source = ctx.create_image({w, h}), destination = ctx.create_image({w, h});
        Pixels input(w * h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float a = (x + y) % 13 == 0 ? 0.f : (x + y) % 5 == 0 ? .5f : 1.f;
                const float v = (x < 128 ? -2.f : 5.f) + ((x + y) % 2 == 0 ? .01f : -.01f);
                input[y * w + x] = {v * a, v * a, v * a, a};
            }
        }
        put(ctx, source, input);
        for (int radius : {10, 100}) {
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.surface_blur(
                    source, destination,
                    test::reserve_workspace(ctx, source,
                                            SurfaceBlurOptions{.radius = radius, .threshold = 15},
                                            surface_blur_requirements));
            });
            const auto actual = get(ctx, destination);
            for (int y = 7; y < h; y += 17) {
                for (int x = 5; x < w; x += 19) {
                    const auto center = input[y * w + x];
                    if (center[3] == 0) {
                        near(actual[y * w + x], {});
                        continue;
                    }
                    long double total = 0, sum = 0;
                    for (int yy = y - radius; yy <= y + radius; ++yy) {
                        for (int xx = x - radius; xx <= x + radius; ++xx) {
                            const auto neighbor =
                                input[std::clamp(yy, 0, h - 1) * w + std::clamp(xx, 0, w - 1)];
                            if (neighbor[3] == 0) {
                                continue;
                            }
                            const long double color = neighbor[0] / neighbor[3],
                                              delta = (color - center[0] / center[3]) / (15. / 255);
                            const long double weight =
                                std::exp(-.5L * ((std::pow(xx - x, 2) + std::pow(yy - y, 2)) /
                                                     (radius * radius / 4.) +
                                                 3 * delta * delta)) *
                                neighbor[3];
                            total += weight;
                            sum += color * weight;
                        }
                    }
                    const float expected = float(sum / total) * center[3];
                    near(actual[y * w + x], {expected, expected, expected, center[3]}, .006f);
                }
            }
        }
    });
    test::run("wide filters apply fractional coverage only at the final write", [] {
        auto ctx = Context::create();
        constexpr int w = 37, h = 29;
        auto source = ctx.create_image({w, h}), full = ctx.create_image({w, h}),
             selected = ctx.create_image({w, h}), scratch = ctx.create_image({w, h}),
             blurred = ctx.create_image({w, h});
        auto mask = ctx.create_mask({w, h});
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.noise(source, {.seed = 17, .monochrome = false});
            cmd.fill(mask, {.coverage = 128.f / 255});
        });
        const Pixel background{-.25f, 2, .125f, 1};
        const auto original = get(ctx, source);
        for (int effect = 0; effect < 9; ++effect) {
            for (bool masked : {false, true}) {
                const auto& output = masked ? selected : full;
                const Mask* coverage = masked ? &mask : nullptr;
                const auto area = masked ? std::optional<Rect>{{9, 3, 17, 11}} : std::nullopt;
                ctx.run_and_wait([&](Commands& cmd) {
                    cmd.fill(output, {.color = {background[0], background[1], background[2], 1}});
                    switch (effect) {
                    case 0:
                        cmd.motion_blur(source, output,
                                        test::reserve_workspace(ctx, source,
                                                                MotionBlurOptions{.angle = 37,
                                                                                  .distance = 2000,
                                                                                  .mask = coverage,
                                                                                  .region = area},
                                                                motion_blur_requirements));
                        break;
                    case 1:
                        cmd.radial_blur(
                            source, output,
                            test::reserve_workspace(
                                ctx, source,
                                RadialBlurOptions{.amount = 100, .mask = coverage, .region = area},
                                radial_blur_requirements));
                        break;
                    case 2:
                        cmd.radial_blur(
                            source, output,
                            test::reserve_workspace(ctx, source,
                                                    RadialBlurOptions{.amount = 100,
                                                                      .mode = RadialBlurMode::zoom,
                                                                      .mask = coverage,
                                                                      .region = area},
                                                    radial_blur_requirements));
                        break;
                    case 3:
                        cmd.surface_blur(
                            source, output,
                            test::reserve_workspace(
                                ctx, source,
                                SurfaceBlurOptions{.radius = 100, .mask = coverage, .region = area},
                                surface_blur_requirements));
                        break;
                    case 4:
                        cmd.minimum(
                            source, output,
                            test::reserve_workspace(
                                ctx, source,
                                MorphologyOptions{.radius = 500, .mask = coverage, .region = area},
                                minimum_requirements));
                        break;
                    case 5:
                        cmd.maximum(source, output,
                                    test::reserve_workspace(
                                        ctx, source,
                                        MorphologyOptions{.radius = 500,
                                                          .shape = MorphologyShape::round,
                                                          .mask = coverage,
                                                          .region = area},
                                        maximum_requirements));
                        break;
                    case 6:
                        cmd.median(source, output,
                                   {.radius = 100, .mask = coverage, .region = area,
                                    .workspace = ctx.create_workspace(median_requirements(source.size(), {.radius = 100}).workspace)});
                        break;
                    case 7:
                        cmd.high_pass(
                            source, output,
                            test::reserve_workspace(
                                ctx, source,
                                HighPassOptions{.radius = 1000, .mask = coverage, .region = area},
                                high_pass_requirements));
                        break;
                    case 8:
                        cmd.copy(source, output);
                        cmd.gaussian_blur(
                            output, test::reserve_workspace(ctx, output,
                                                            GaussianBlurOptions{.radius = 1000,
                                                                                .sigma = 333,
                                                                                .mask = coverage,
                                                                                .region = area},
                                                            gaussian_blur_requirements));
                        break;
                    }
                });
            }
            const auto filtered = get(ctx, full), actual = get(ctx, selected);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    Pixel expected = effect == 8 ? original[y * w + x] : background;
                    if (x >= 9 && x < 26 && y >= 3 && y < 14) {
                        for (int c = 0; c < 4; ++c) {
                            expected[c] += (filtered[y * w + x][c] - expected[c]) * (128.f / 255);
                        }
                    }
                    near(actual[y * w + x], expected, 3e-5f);
                }
            }
        }
    });
    test::run("wide Gaussian retains finite HDR extrema and clamped boundary asymptotes", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({37, 19}), scratch = ctx.create_image({37, 19});
        const float peak = std::numeric_limits<float>::max();
        put(ctx, image, Pixels(37 * 19, Pixel{peak, -peak, .01f, 1}));
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.gaussian_blur(
                image, test::reserve_workspace(ctx, image,
                                               GaussianBlurOptions{.radius = 500, .sigma = 167},
                                               gaussian_blur_requirements));
        });
        for (auto p : get(ctx, image)) {
            test::near(p[0] / peak, 1, 2e-5f);
            test::near(p[1] / peak, -1, 2e-5f);
            test::near(p[2], .01f);
        }
        Pixels impulse(37 * 19, Pixel{0, 0, 0, 1});
        impulse[0] = {1, 1, 1, 1};
        put(ctx, image, impulse);
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.gaussian_blur(image, test::reserve_workspace(
                                         ctx, image,
                                         GaussianBlurOptions{.radius = 1000000, .sigma = 1000000},
                                         gaussian_blur_requirements));
        });
        // With a clamped corner impulse, half the infinite samples on each axis
        // see the corner; the two-dimensional limiting value is one quarter.
        for (auto p : get(ctx, image)) {
            test::near(p[0], .25f, .005f);
        }
    });
    test::run("Gaussian capacity queries agree at effective-support rounding boundaries", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({37, 19}), scratch = ctx.create_image({37, 19});
        ctx.run_and_wait([&](Commands& cmd) { cmd.fill(image, {.color = {2, -1, .5f, 1}}); });
        const GaussianBlurOptions options{.radius = 500, .sigma = 16.01f};
        auto cmd = ctx.create_commands();
        cmd.gaussian_blur(image,
                          test::reserve_workspace(ctx, image, options, gaussian_blur_requirements));
        ctx.submit_and_wait(cmd);
        for (auto p : get(ctx, image)) {
            near(p, {2, -1, .5f, 1});
        }
    });
    test::run("unsharp threshold extends continuously to negative linear colors", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({37, 19}), destination = ctx.create_image({37, 19}),
             scratch = ctx.create_image({37, 19}), blurred = ctx.create_image({37, 19});
        Pixels input(37 * 19);
        for (int y = 0; y < 19; ++y) {
            for (int x = 0; x < 37; ++x) {
                input[y * 37 + x] = {x < 18 ? -.2f : -.1f, 0, 0, 1};
            }
        }
        put(ctx, source, input);
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.unsharp_mask(source, destination,
                             test::reserve_workspace(
                                 ctx, source, UnsharpMaskOptions{.radius = 1, .threshold = 5},
                                 unsharp_mask_requirements));
        });
        const auto actual = get(ctx, destination);
        test::check(actual[9 * 37 + 17][0] < -.2f && actual[9 * 37 + 18][0] > -.1f,
                    "negative edge was protected by clipping the threshold encoding");
    });
    test::run("exact median preserves smooth ramps", [] {
        auto ctx = Context::create();
        constexpr int w = 2059, h = 17;
        auto source = ctx.create_image({w, h}), destination = ctx.create_image({w, h});
        Pixels input(w * h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                input[y * w + x] = {-2 + 9 * float(x) / (w - 1), .25f, .5f, 1};
            }
        }
        put(ctx, source, input);
        auto cmd = ctx.create_commands();
        cmd.median(source, destination, {.radius = 100,
            .workspace = ctx.create_workspace(median_requirements(source.size(), {.radius = 100}).workspace)});
        ctx.submit_and_wait(cmd);
        const auto actual = get(ctx, destination);
        for (int y = 0; y < h; ++y) {
            for (int x = 150; x < w - 150; ++x) {
                near(actual[y * w + x], input[y * w + x], 9.f / (w - 1));
            }
        }
    });
    test::run("exact median retains tiny positive alpha and its HDR color", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({37, 19}), destination = ctx.create_image({37, 19});
        Pixels input(37 * 19, Pixel{1, -2, .5f, 1e-20f});
        input.front() = {0, 0, 0, 0};
        input.back() = {1, -2, .5f, 1};
        put(ctx, source, input);
        ctx.run_and_wait([&](Commands& cmd) { cmd.median(source, destination, {.radius = 2}); });
        // Even the clamped corner contributes only nine of the 25 samples.
        for (auto p : get(ctx, destination)) {
            test::near(p[0], 1);
            test::near(p[1], -2);
            test::near(p[2], .5f);
            test::near(p[3] / 1e-20f, 1, 2e-5f);
        }
    });
    return test::finish();
}
