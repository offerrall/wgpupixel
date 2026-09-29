#include "../test.h"
#include <array>
#include <cstring>
#include <limits>
#include <numbers>
using namespace wgpupixel;
namespace {
using Pixel = std::array<double, 4>;
constexpr int width = 7, height = 5;
using Pixels = std::vector<Pixel>;
Pixel sample(const Pixels& pixels, int x, int y) {
    return pixels[std::clamp(y, 0, height - 1) * width + std::clamp(x, 0, width - 1)];
}
Pixel straight(Pixel p) {
    for (int c = 0; c < 3; ++c) {
        p[c] = p[3] > 0 ? p[c] / p[3] : 0;
    }
    return p;
}
Pixel linear(const Pixels& pixels, double x, double y) {
    x = std::clamp(x - .5, 0., double(width - 1));
    y = std::clamp(y - .5, 0., double(height - 1));
    const int ix = int(x), iy = int(y);
    Pixel result{};
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            const auto p = sample(pixels, ix + i, iy + j);
            const double weight = (i ? x - ix : 1 - x + ix) * (j ? y - iy : 1 - y + iy);
            for (int c = 0; c < 4; ++c) {
                result[c] += p[c] * weight;
            }
        }
    }
    return result;
}
std::uint32_t hash(std::uint32_t value) {
    value = (value ^ (value >> 16)) * 0x7feb352du;
    value = (value ^ (value >> 15)) * 0x846ca68bu;
    return value ^ (value >> 16);
}
double random(std::uint32_t key) {
    return double(hash(key) >> 8) / 16777216.;
}
double encoded(double value) {
    value = std::max(value, 0.0);
    return value <= .0031308 ? value * 12.92 : 1.055 * std::pow(value, 1 / 2.4) - .055;
}
Pixels reference(const Pixels& source, int mode, int radius, bool alternate) {
    Pixels result(source.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int index = y * width + x;
            const auto center = straight(source[index]);
            Pixel out{};
            if (mode == 10 && source[index][3] == 0) {
                result[index] = {};
                continue;
            }
            if (mode == 1 || mode == 2) {
                const double dx = x + .5 - 3.2, dy = y + .5 - 2.1;
                const double extent =
                    std::hypot(dx, dy) * (alternate ? .25 : 25 * std::numbers::pi / 180);
                const int count =
                    mode == 1 ? 9 : std::clamp(int(std::ceil(extent * 2)) + 1, 2, 1024);
                for (int i = 0; i < count; ++i) {
                    double xx, yy, t = double(i) / (count - 1);
                    if (mode == 1) {
                        xx = x + .5 + (t - .5) * 4 * std::cos(.4);
                        yy = y + .5 + (t - .5) * 4 * std::sin(.4);
                    } else if (alternate) {
                        xx = x + .5 - dx * t * .25;
                        yy = y + .5 - dy * t * .25;
                    } else {
                        const double a = (t - .5) * 25 * std::numbers::pi / 180;
                        xx = 3.2 + dx * std::cos(a) - dy * std::sin(a);
                        yy = 2.1 + dx * std::sin(a) + dy * std::cos(a);
                    }
                    auto p = linear(source, xx, yy);
                    for (int c = 0; c < 4; ++c) {
                        out[c] += p[c] / count;
                    }
                }
            } else if (mode == 9) {
                out = source[index];
                const std::uint32_t key =
                    std::uint32_t(x) * 0x1f123bb5u ^ std::uint32_t(y) * 0x5f356495u ^ 42u;
                for (int c = 0; c < 3; ++c) {
                    const auto k = key + (alternate ? std::uint32_t(c) * 0x9e3779b9u : 0u);
                    double noise = 2 * random(k) - 1;
                    if (alternate) {
                        noise = std::sqrt(-2 * std::log(std::max(random(k), 1. / 16777216))) *
                                std::cos(2 * std::numbers::pi * random(k ^ 0xa511e9b3u));
                    }
                    out[c] += noise * .2 * out[3];
                }
            } else {
                const int support = mode == 3 || mode == 4 ? 3 * radius : radius;
                std::array<std::vector<double>, 4> values;
                double total = 0;
                for (int j = -support; j <= support; ++j) {
                    for (int i = -support; i <= support; ++i) {
                        if ((mode == 6 || mode == 7) && alternate &&
                            i * i + j * j > radius * radius) {
                            continue;
                        }
                        int xx = x + i, yy = y + j;
                        if (mode == 8) {
                            if (i < 0 || j < 0 || i >= radius || j >= radius) {
                                continue;
                            }
                            xx = x / radius * radius + i;
                            yy = y / radius * radius + j;
                            if (xx >= width || yy >= height) {
                                continue;
                            }
                        }
                        auto p = sample(source, xx, yy);
                        const auto color = straight(p);
                        double weight = 1;
                        if (mode == 3 || mode == 4) {
                            weight =
                                radius ? std::exp(-.5 * (i * i + j * j) / (radius * radius)) : 1;
                        }
                        if (mode == 10) {
                            double difference = 0;
                            for (int c = 0; c < 3; ++c) {
                                difference += std::pow((color[c] - center[c]) / (80. / 255), 2);
                            }
                            const double sigma = std::max(radius / 2., .5);
                            weight =
                                std::exp(-.5 * ((i * i + j * j) / (sigma * sigma) + difference));
                        }
                        for (int c = 0; c < 4; ++c) {
                            out[c] += p[c] * weight;
                        }
                        total += mode == 10 ? weight * p[3] : weight;
                        for (int c = 0; c < 4; ++c) {
                            values[c].push_back(p[c]);
                        }
                    }
                }
                for (auto& c : out) {
                    c /= total;
                }
                if (mode == 10) {
                    for (int c = 0; c < 3; ++c) {
                        out[c] *= source[index][3];
                    }
                    out[3] = source[index][3];
                }
                if (mode == 3 || mode == 4) {
                    const auto low = straight(out);
                    for (int c = 0; c < 3; ++c) {
                        const double difference = center[c] - low[c];
                        out[c] = mode == 4 ? .5 + difference
                                           : center[c] + (std::abs(encoded(center[c]) -
                                                                   encoded(low[c])) >= 10. / 255
                                                              ? 1.5 * difference
                                                              : 0);
                        out[c] *= source[index][3];
                    }
                    out[3] = source[index][3];
                }
                if (mode >= 5 && mode <= 7) {
                    for (int c = 0; c < 4; ++c) {
                        std::ranges::sort(values[c]);
                        out[c] = values[c][mode == 5   ? values[c].size() / 2
                                           : mode == 6 ? 0
                                                       : values[c].size() - 1];
                    }
                    if (out[3] == 0) {
                        out = {};
                    }
                }
            }
            result[index] = out;
        }
    }
    return result;
}
void apply(Context& ctx, Commands& cmd, int mode, int radius, bool alternate, const Image& source,
           const Image& destination, const Image& scratch, const Image& blurred, const Mask* mask,
           std::optional<Rect> region) {
    switch (mode) {
    case 0:
        cmd.box_blur(source, destination,
                     test::reserve_workspace(
                         ctx, source,
                         BoxBlurOptions{.radius = radius, .mask = mask, .region = region},
                         box_blur_requirements));
        break;
    case 1:
        cmd.motion_blur(
            source, destination,
            test::reserve_workspace(ctx, source,
                                    MotionBlurOptions{.angle = float(.4 * 180 / std::numbers::pi),
                                                      .distance = 4,
                                                      .mask = mask,
                                                      .region = region},
                                    motion_blur_requirements));
        break;
    case 2:
        cmd.radial_blur(
            source, destination,
            test::reserve_workspace(
                ctx, source,
                RadialBlurOptions{.center = {3.2f, 2.1f},
                                  .amount = 25,
                                  .mode = alternate ? RadialBlurMode::zoom : RadialBlurMode::spin,
                                  .mask = mask,
                                  .region = region},
                radial_blur_requirements));
        break;
    case 3:
        cmd.unsharp_mask(source, destination,
                         test::reserve_workspace(ctx, source,
                                                 UnsharpMaskOptions{.amount = 150,
                                                                    .radius = float(radius),
                                                                    .threshold = 10,
                                                                    .mask = mask,
                                                                    .region = region},
                                                 unsharp_mask_requirements));
        break;
    case 4:
        cmd.high_pass(source, destination,
                      test::reserve_workspace(
                          ctx, source,
                          HighPassOptions{.radius = float(radius), .mask = mask, .region = region},
                          high_pass_requirements));
        break;
    case 5:
        cmd.median(source, destination, {.radius = radius, .mask = mask, .region = region});
        break;
    case 6:
        cmd.minimum(
            source, destination,
            test::reserve_workspace(ctx, source,
                                    MorphologyOptions{.radius = radius,
                                                      .shape = alternate ? MorphologyShape::round
                                                                         : MorphologyShape::square,
                                                      .mask = mask,
                                                      .region = region},
                                    minimum_requirements));
        break;
    case 7:
        cmd.maximum(
            source, destination,
            test::reserve_workspace(ctx, source,
                                    MorphologyOptions{.radius = radius,
                                                      .shape = alternate ? MorphologyShape::round
                                                                         : MorphologyShape::square,
                                                      .mask = mask,
                                                      .region = region},
                                    maximum_requirements));
        break;
    case 8:
        cmd.pixelate(source, destination, {.cell_size = radius, .mask = mask, .region = region});
        break;
    case 9:
        cmd.add_noise(destination, {.amount = 20,
                                    .distribution = alternate ? NoiseDistribution::gaussian
                                                              : NoiseDistribution::uniform,
                                    .monochrome = !alternate,
                                    .clip = false,
                                    .seed = 42,
                                    .mask = mask,
                                    .region = region});
        break;
    case 10:
        cmd.surface_blur(source, destination,
                         test::reserve_workspace(
                             ctx, source,
                             SurfaceBlurOptions{
                                 .radius = radius, .threshold = 80, .mask = mask, .region = region},
                             surface_blur_requirements));
        break;
    }
}
} // namespace
int main() {
    const std::array names{"box_blur",  "motion_blur", "radial_blur", "unsharp_mask",
                           "high_pass", "median",      "minimum",     "maximum",
                           "pixelate",  "add_noise",   "surface_blur"};
    for (int mode = 0; mode < 11; ++mode) {
        test::run(names[mode], [&] {
            auto ctx = Context::create();
            auto source = ctx.create_image({width, height}),
                 destination = ctx.create_image({width, height}),
                 scratch = ctx.create_image({width, height}),
                 blurred = ctx.create_image({width, height});
            auto upload =
                ctx.create_upload_buffer(source, {.format = TransferFormat::rgba32_float});
            auto readback =
                ctx.create_readback_buffer(destination, {.format = TransferFormat::rgba32_float});
            auto mask = ctx.create_mask({width, height});
            auto mask_upload = ctx.create_upload_buffer(mask);
            std::vector<std::uint8_t> coverage(width * height);
            Pixels pixels(width * height);
            std::vector<float> input(width * height * 4), actual(input.size());
            for (int i = 0; i < width * height; ++i) {
                coverage[i] = std::array<std::uint8_t, 4>{0, 64, 128, 255}[i % 4];
                const float alpha = float(i % 5) / 4;
                for (int c = 0; c < 4; ++c) {
                    input[i * 4 + c] =
                        c == 3 ? alpha : (float((i * 7 + c * 3) % 19) / 10 - .3f) * alpha;
                }
                for (int c = 0; c < 4; ++c) {
                    pixels[i][c] = input[i * 4 + c];
                }
            }
            ctx.write(upload, {reinterpret_cast<const std::uint8_t*>(input.data()),
                               input.size() * sizeof(float)});
            ctx.write(mask_upload, coverage);
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.upload(upload, source);
                cmd.upload(mask_upload, mask);
            });
            for (bool alternate : {false, true}) {
                for (bool selected : {false, true}) {
                    for (int radius : {mode == 8 ? 1 : 0, mode == 5 ? 1 : 2}) {
                        auto expected = reference(pixels, mode, radius, alternate);
                        ctx.run_and_wait([&](Commands& cmd) {
                            cmd.copy(source, destination);
                            apply(ctx, cmd, mode, radius, alternate, source, destination, scratch,
                                  blurred, selected ? &mask : nullptr,
                                  selected ? std::optional<Rect>{{1, -1, 5, 4}} : std::nullopt);
                            cmd.download(destination, readback);
                        });
                        ctx.read(readback, {reinterpret_cast<std::uint8_t*>(actual.data()),
                                            actual.size() * sizeof(float)});
                        for (int i = 0; i < width * height; ++i) {
                            for (int c = 0; c < 4; ++c) {
                                const double amount =
                                    selected ? (i % width >= 1 && i % width < 6 && i / width < 3
                                                    ? coverage[i] / 255.
                                                    : 0)
                                             : 1;
                                test::near(
                                    actual[i * 4 + c],
                                    float(pixels[i][c] * (1 - amount) + expected[i][c] * amount),
                                    2e-4f);
                            }
                        }
                    }
                }
            }
        });
    }
    test::run("filter identities and empty selections", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({1, 1}), destination = ctx.create_image({1, 1}),
             scratch = ctx.create_image({1, 1}), blurred = ctx.create_image({1, 1});
        auto cmd = ctx.create_commands(16);
        cmd.fill(source, {.color = {.1f, .2f, .3f, .5f}});
        cmd.motion_blur(source, destination,
                        test::reserve_workspace(ctx, source, MotionBlurOptions{.distance = 0},
                                                motion_blur_requirements));
        cmd.radial_blur(destination, source,
                        test::reserve_workspace(
                            ctx, destination, RadialBlurOptions{.center = {.5f, .5f}, .amount = 0},
                            radial_blur_requirements));
        cmd.surface_blur(source, destination,
                         test::reserve_workspace(ctx, source,
                                                 SurfaceBlurOptions{.radius = 3, .threshold = 0},
                                                 surface_blur_requirements));
        cmd.unsharp_mask(destination, source,
                         test::reserve_workspace(ctx, destination,
                                                 UnsharpMaskOptions{.amount = 0, .radius = 4},
                                                 unsharp_mask_requirements));
        cmd.add_noise(source, {.amount = 0});
        cmd.box_blur(source, destination,
                     test::reserve_workspace(ctx, source, BoxBlurOptions{.radius = 12},
                                             box_blur_requirements));
        ctx.submit_and_wait(cmd);
        auto expected = test::read(ctx, source);
        auto actual = test::read(ctx, destination);
        test::check(actual == expected, "zero controls or constant clamp edges changed the image");
        cmd.fill(destination, {.color = {1, 0, 0, 1}});
        const Rect empty{0, 0, 0, 1};
        cmd.pixelate(source, destination, {.cell_size = 1024, .region = empty});
        cmd.high_pass(source, destination,
                      test::reserve_workspace(ctx, source, HighPassOptions{.region = empty},
                                              high_pass_requirements));
        cmd.median(source, destination, {.radius = 1, .region = empty});
        ctx.submit_and_wait(cmd);
        test::check(test::read(ctx, destination) == std::vector<std::uint8_t>{255, 0, 0, 255},
                    "empty selection wrote pixels");
    });
    test::run("Gaussian finite float32 limits", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 1}), scratch = ctx.create_image({3, 1});
        auto upload = ctx.create_upload_buffer(image, {.format = TransferFormat::rgba32_float});
        auto readback = ctx.create_readback_buffer(image, {.format = TransferFormat::rgba32_float});
        const float largest = std::numeric_limits<float>::max();
        for (bool mixed : {false, true}) {
            std::array<float, 12> input{
                largest, 1e-20f, 0, 1e-20f, mixed ? -largest : largest, 1e-20f, 0, 1e-20f,
                largest, 1e-20f, 0, 1e-20f},
                actual{};
            ctx.write(upload, {reinterpret_cast<const std::uint8_t*>(input.data()), sizeof(input)});
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.upload(upload, image);
                cmd.gaussian_blur(
                    image, test::reserve_workspace(ctx, image,
                                                   GaussianBlurOptions{.radius = 3, .sigma = 1.5f},
                                                   gaussian_blur_requirements));
                cmd.download(image, readback);
            });
            ctx.read(readback, {reinterpret_cast<std::uint8_t*>(actual.data()), sizeof(actual)});
            for (int x = 0; x < 3; ++x) {
                double sum = 0, total = 0;
                for (int offset = -3; offset <= 3; ++offset) {
                    const double weight = std::exp(-.5 * offset * offset / (1.5 * 1.5));
                    sum += double(input[std::clamp(x + offset, 0, 2) * 4]) * weight;
                    total += weight;
                }
                test::near(actual[x * 4] / largest, float(sum / total / largest), 2e-6f);
                test::near(actual[x * 4 + 1] / 1e-20f, 1, 2e-6f);
                test::near(actual[x * 4 + 3] / 1e-20f, 1, 2e-6f);
            }
        }
    });
    test::run("filter validation and requirements", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({7, 5}), destination = ctx.create_image({7, 5}),
             scratch = ctx.create_image({7, 5}), blurred = ctx.create_image({7, 5}),
             small = ctx.create_image({1, 1});
        auto mask = ctx.create_mask({1, 1});
        auto cmd = ctx.create_commands(3);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        auto invalid = [&](std::string_view name, std::string_view parameter, auto operation) {
            test::error(ErrorCode::invalid_argument, name, parameter, operation);
        };
        invalid("box_blur", "radius", [&] { cmd.box_blur(source, destination, {.radius = -1}); });
        invalid("motion_blur", "angle",
                [&] { cmd.motion_blur(source, destination, {.angle = nan}); });
        invalid("motion_blur", "distance",
                [&] { cmd.motion_blur(source, destination, {.distance = 4097}); });
        invalid("radial_blur", "center",
                [&] { cmd.radial_blur(source, destination, {.center = {nan, 0}}); });
        invalid("radial_blur", "mode",
                [&] { cmd.radial_blur(source, destination, {.mode = RadialBlurMode(9)}); });
        invalid("unsharp_mask", "threshold",
                [&] { cmd.unsharp_mask(source, destination, {.threshold = nan}); });
        invalid("unsharp_mask", "amount",
                [&] { cmd.unsharp_mask(source, destination, {.amount = 501}); });
        invalid("high_pass", "radius",
                [&] { cmd.high_pass(source, destination, {.radius = 1001}); });
        invalid("median", "radius", [&] { cmd.median(source, destination, {.radius = 501}); });
        invalid("minimum", "shape",
                [&] { cmd.minimum(source, destination, {.shape = MorphologyShape(9)}); });
        invalid("maximum", "radius", [&] { cmd.maximum(source, destination, {.radius = 501}); });
        invalid("pixelate", "cell_size",
                [&] { cmd.pixelate(source, destination, {.cell_size = 0}); });
        invalid("add_noise", "distribution",
                [&] { cmd.add_noise(destination, {.distribution = NoiseDistribution(9)}); });
        invalid("surface_blur", "threshold",
                [&] { cmd.surface_blur(source, destination, {.threshold = -1}); });
        for (int effect = 0; effect < 3; ++effect) {
            const auto name = std::array{"box_blur", "unsharp_mask", "high_pass"}[effect];
            test::error(ErrorCode::capacity, name, "workspace", [&] {
                if (effect == 0) {
                    cmd.box_blur(source, destination);
                }
                if (effect == 1) {
                    cmd.unsharp_mask(source, destination);
                }
                if (effect == 2) {
                    cmd.high_pass(source, destination);
                }
            });
        }
        invalid("box_blur", "mask", [&] { cmd.box_blur(source, destination, {.mask = &mask}); });
        invalid("pixelate", "region",
                [&] { cmd.pixelate(source, destination, {.region = Rect{0, 0, -1, 1}}); });
        for (int mode = 0; mode < 11; ++mode) {
            if (mode == 9) {
                continue;
            }
            const std::array names{"box_blur",  "motion_blur", "radial_blur", "unsharp_mask",
                                   "high_pass", "median",      "minimum",     "maximum",
                                   "pixelate",  "add_noise",   "surface_blur"};
            invalid(names[mode], "destination", [&] {
                apply(ctx, cmd, mode, 1, false, source, source, scratch, blurred, nullptr, {});
            });
        }

        test::check(box_blur_requirements({7, 5}, {}).workspace.bytes() == 7 * 5 * 16 &&
                        unsharp_mask_requirements({7, 5}, {}).workspace.bytes() == 2 * 7 * 5 * 16,
                    "small filters require one or two full-size workspace planes");
        test::check(minimum_requirements({7, 5}, {}).destination == ImageSize{7, 5},
                    "minimum preserves dimensions");
        test::check(high_pass_requirements({7, 5}, {}).workspace.bytes() == 2 * 7 * 5 * 16,
                    "high pass needs two full-size planes");
        invalid("box_blur_requirements", "source",
                [&] { (void)box_blur_requirements({0, 5}, {}); });
        invalid("high_pass_requirements", "radius",
                [&] { (void)high_pass_requirements({7, 5}, {.radius = nan}); });
        // All rejected calls leave the original three slots available.
        cmd.fill(source, {.color = {.2f, .3f, .4f, 1}});
        cmd.box_blur(source, destination,
                     test::reserve_workspace(ctx, source, BoxBlurOptions{}, box_blur_requirements));
        ctx.submit_and_wait(cmd);
        auto limited = ctx.create_commands(2);
        auto high_options =
            test::reserve_workspace(ctx, source, HighPassOptions{}, high_pass_requirements);
        ctx.set_memory_limit(ctx.memory().total);
        test::error(ErrorCode::capacity, "high_pass", "memory_limit",
                    [&] { limited.high_pass(source, destination, high_options); });
        ctx.set_memory_limit(0);
        limited.box_blur(
            source, destination,
            test::reserve_workspace(ctx, source, BoxBlurOptions{}, box_blur_requirements));
        ctx.submit_and_wait(limited);
    });
    return test::finish();
}
