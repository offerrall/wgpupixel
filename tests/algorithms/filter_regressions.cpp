#include "../test.h"
#include <array>
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
    test::run("median majority and morphology geometry across workgroups", [] {
        auto ctx = Context::create();
        constexpr int w = 37, h = 29, left = 11, right = 26, top = 9, bottom = 21;
        auto source = ctx.create_image({w, h}), destination = ctx.create_image({w, h}),
             scratch = ctx.create_image({w, h});
        Pixels input(w * h);
        for (int y = top; y < bottom; ++y) {
            for (int x = left; x < right; ++x) {
                input[y * w + x] = {1, 1, 1, 1};
            }
        }
        put(ctx, source, input);
        for (auto shape : {MorphologyShape::square, MorphologyShape::round}) {
            for (bool maximum : {false, true}) {
                ctx.run_and_wait([&](Commands& cmd) {
                    if (maximum) {
                        cmd.maximum(source, destination,
                                    test::reserve_workspace(
                                        ctx, source, MorphologyOptions{.radius = 3, .shape = shape},
                                        maximum_requirements));
                    } else {
                        cmd.minimum(source, destination,
                                    test::reserve_workspace(
                                        ctx, source, MorphologyOptions{.radius = 3, .shape = shape},
                                        minimum_requirements));
                    }
                });
                const auto actual = get(ctx, destination);
                for (int y = 0; y < h; ++y) {
                    for (int x = 0; x < w; ++x) {
                        const int dx = std::max({left - x, 0, x - (right - 1)}),
                                  dy = std::max({top - y, 0, y - (bottom - 1)});
                        const bool inside =
                            maximum
                                ? (shape == MorphologyShape::square ? std::max(dx, dy) <= 3
                                                                    : dx * dx + dy * dy <= 9)
                                : x >= left + 3 && x < right - 3 && y >= top + 3 && y < bottom - 3;
                        const float v = inside ? 1 : 0;
                        near(actual[y * w + x], {v, v, v, v});
                    }
                }
            }
        }
        ctx.run_and_wait([&](Commands& cmd) { cmd.median(source, destination); });
        const auto actual = get(ctx, destination);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const bool corner = (x == left || x == right - 1) && (y == top || y == bottom - 1);
                const float v = x >= left && x < right && y >= top && y < bottom && !corner ? 1 : 0;
                near(actual[y * w + x], {v, v, v, v});
            }
        }
        input.assign(w * h, {});
        for (int x = 0; x < w; ++x) {
            input[14 * w + x] = {1, 1, 1, 1};
        }
        put(ctx, source, input);
        ctx.run_and_wait([&](Commands& cmd) { cmd.median(source, destination); });
        for (auto p : get(ctx, destination)) {
            near(p, {});
        }
    });
    test::run("running box and parallel mosaic have analytic ramp averages", [] {
        auto ctx = Context::create();
        constexpr int w = 259, h = 131;
        auto source = ctx.create_image({w, h}), destination = ctx.create_image({w, h}),
             scratch = ctx.create_image({w, h});
        auto mask = ctx.create_mask({w, h});
        ctx.run_and_wait([&](Commands& cmd) { cmd.fill(mask, {.coverage = 128.f / 255}); });
        Pixels input(w * h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                input[y * w + x] = {float(x) / (w - 1), float(y) / (h - 1), .25f, 1};
            }
        }
        put(ctx, source, input);
        const Rect region{13, 9, 183, 107};
        const Pixel background{.125f, .25f, .5f, .75f};
        for (bool mosaic : {false, true}) {
            for (int parameter : {1, 3, 128, 1024}) {
                ctx.run_and_wait([&](Commands& cmd) {
                    cmd.fill(destination, {.color = {background[0], background[1], background[2],
                                                     background[3]}});
                    if (mosaic) {
                        cmd.pixelate(source, destination,
                                     {.cell_size = parameter, .mask = &mask, .region = region});
                    } else {
                        cmd.box_blur(source, destination,
                                     test::reserve_workspace(ctx, source,
                                                             BoxBlurOptions{.radius = parameter,
                                                                            .mask = &mask,
                                                                            .region = region},
                                                             box_blur_requirements));
                    }
                });
                const auto actual = get(ctx, destination);
                auto average = [&](int p, int size) {
                    if (mosaic) {
                        const int begin = p / parameter * parameter,
                                  end = std::min(begin + parameter, size);
                        return float(begin + end - 1) / (2 * (size - 1));
                    }
                    const int begin = std::max(0, p - parameter),
                              end = std::min(size - 1, p + parameter);
                    const double sum = double(begin + end) * (end - begin + 1) / 2 +
                                       double(std::max(0, p + parameter - size + 1)) * (size - 1);
                    return float(sum / ((2 * parameter + 1.) * (size - 1)));
                };
                for (int y = 0; y < h; ++y) {
                    for (int x = 0; x < w; ++x) {
                        Pixel expected = background;
                        if (x >= 13 && x < 196 && y >= 9 && y < 116) {
                            const Pixel filtered{average(x, w), average(y, h), .25f, 1};
                            for (int c = 0; c < 4; ++c) {
                                expected[c] += (filtered[c] - expected[c]) * (128.f / 255);
                            }
                        }
                        near(actual[y * w + x], expected, 2e-4f);
                    }
                }
            }
        }
    });
    test::run("centered motion preserves an affine ramp away from image edges", [] {
        auto ctx = Context::create();
        constexpr int w = 73, h = 55;
        auto source = ctx.create_image({w, h}), destination = ctx.create_image({w, h});
        Pixels input(w * h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                input[y * w + x] = {float(x + y) / (w + h), float(x) / w, .25f, 1};
            }
        }
        put(ctx, source, input);
        for (float angle : {0.f, 37.f, 90.f}) {
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.copy(source, destination);
                cmd.motion_blur(source, destination,
                                test::reserve_workspace(
                                    ctx, source,
                                    MotionBlurOptions{.angle = angle,
                                                      .distance = 32,
                                                      .region = Rect{17, 17, w - 34, h - 34}},
                                    motion_blur_requirements));
            });
            const auto actual = get(ctx, destination);
            for (std::size_t i = 0; i < actual.size(); ++i) {
                near(actual[i], input[i]);
            }
        }
    });
    test::run("threshold protects small highlight steps but sharpens large shadow steps", [] {
        auto ctx = Context::create();
        constexpr int w = 37, h = 17;
        auto source = ctx.create_image({w, h}), destination = ctx.create_image({w, h}),
             scratch = ctx.create_image({w, h}), blurred = ctx.create_image({w, h});
        for (bool shadow : {false, true}) {
            const float a = decode((shadow ? 10.f : 235.f) / 255),
                        b = decode((shadow ? 60.f : 245.f) / 255);
            Pixels input(w * h);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    float v = x < 18 ? a : b;
                    input[y * w + x] = {v, v, v, 1};
                }
            }
            put(ctx, source, input);
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.unsharp_mask(source, destination,
                                 test::reserve_workspace(
                                     ctx, source,
                                     UnsharpMaskOptions{.amount = 100, .radius = 1, .threshold = 5},
                                     unsharp_mask_requirements));
            });
            const auto actual = get(ctx, destination);
            if (shadow) {
                test::check(actual[8 * w + 18][0] > b + .001f,
                            "50-level shadow edge was not sharpened");
                test::check(actual[8 * w + 17][0] < a - .001f,
                            "shadow edge's dark side was not sharpened");
            } else {
                for (std::size_t i = 0; i < actual.size(); ++i) {
                    near(actual[i], input[i]);
                }
            }
        }
    });
    test::run("surface blur preserves coverage and constant object colors", [] {
        auto ctx = Context::create();
        constexpr int w = 35, h = 19;
        auto source = ctx.create_image({w, h}), destination = ctx.create_image({w, h});
        for (Pixel color : {Pixel{0, 0, 0, 1}, Pixel{1, 1, 1, 1}, Pixel{.1f, .7f, .3f, 1}}) {
            Pixels input(w * h);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const float alpha = std::array{0.f, .25f, 1.f}[(x + y) % 3];
                    input[y * w + x] = {color[0] * alpha, color[1] * alpha, color[2] * alpha,
                                        alpha};
                }
            }
            put(ctx, source, input);
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.surface_blur(source, destination,
                                 test::reserve_workspace(
                                     ctx, source, SurfaceBlurOptions{.radius = 3, .threshold = 15},
                                     surface_blur_requirements));
            });
            const auto actual = get(ctx, destination);
            for (std::size_t i = 0; i < actual.size(); ++i) {
                near(actual[i], input[i]);
            }
        }
    });
    test::run("tiled Gaussian impulse and legacy no-data kernels", [] {
        auto ctx = Context::create();
        // Drop shadow records the original blur kernels without staging kernel data.
        auto stamp = ctx.create_image({1, 1}), composite = ctx.create_image({1, 1}),
             shadow = ctx.create_image({3, 3}), shadow_scratch = ctx.create_image({3, 3});
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.fill(stamp, {.color = {.25f, .5f, .75f, 1}});
            cmd.drop_shadow(
                stamp, composite,
                test::reserve_workspace(
                    ctx, stamp,
                    DropShadowOptions{.radius = 1, .color = {0, 0, 0, .5f}, .expand = false},
                    drop_shadow_requirements));
        });
        near(get(ctx, composite)[0], {.25f, .5f, .75f, 1});
        constexpr int w = 37, h = 29;
        auto image = ctx.create_image({w, h}), scratch = ctx.create_image({w, h});
        auto mask = ctx.create_mask({w, h});
        ctx.run_and_wait([&](Commands& cmd) { cmd.fill(mask, {.coverage = 128.f / 255}); });
        Pixels input(w * h, Pixel{0, .25f, .5f, 1});
        input[13 * w + 18][0] = 1;
        for (int radius : {8, 32, 65}) {
            const double sigma = radius / 3.;
            put(ctx, image, input);
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.gaussian_blur(image, test::reserve_workspace(
                                             ctx, image,
                                             GaussianBlurOptions{.radius = radius,
                                                                 .sigma = float(sigma),
                                                                 .mask = &mask,
                                                                 .region = Rect{9, 7, 21, 17}},
                                             gaussian_blur_requirements));
            });
            const auto actual = get(ctx, image);
            double normalization = 0;
            for (int i = -radius; i <= radius; ++i) {
                normalization += std::exp(-.5 * i * i / (sigma * sigma));
            }
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    Pixel expected = input[y * w + x];
                    if (x >= 9 && x < 30 && y >= 7 && y < 24) {
                        const int dx = x - 18, dy = y - 13;
                        const float convolution =
                            std::abs(dx) <= radius && std::abs(dy) <= radius
                                ? float(std::exp(-.5 * (dx * dx + dy * dy) / (sigma * sigma)) /
                                        (normalization * normalization))
                                : 0;
                        expected[0] += (convolution - expected[0]) * (128.f / 255);
                    }
                    near(actual[y * w + x], expected);
                }
            }
        }
    });
    test::run("tiled dispatch falls back for long thin images", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({1, 262145}), scratch = ctx.create_image({1, 262145});
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.fill(source, {.color = {.125f, .25f, .5f, 1}});
            cmd.gaussian_blur(source,
                              test::reserve_workspace(ctx, source, GaussianBlurOptions{.radius = 2},
                                                      gaussian_blur_requirements));
        });
        for (auto p : get(ctx, source)) {
            near(p, {.125f, .25f, .5f, 1});
        }
    });
    test::run("documented parameter ranges and explicit noise clipping", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({37, 29}), destination = ctx.create_image({37, 29}),
             scratch = ctx.create_image({37, 29});
        auto cmd = ctx.create_commands();
        auto error = [&](std::string_view operation, std::string_view parameter, auto call) {
            test::error(ErrorCode::invalid_argument, operation, parameter, call);
        };
        error("median", "radius", [&] { cmd.median(source, destination, {.radius = 501}); });
        error("surface_blur", "radius",
              [&] { cmd.surface_blur(source, destination, {.radius = 101}); });
        error("maximum", "radius", [&] { cmd.maximum(source, destination, {.radius = 501}); });
        error("motion_blur", "distance",
              [&] { cmd.motion_blur(source, destination, {.distance = 4097}); });
        test::check(!AddNoiseOptions{}.monochrome && !AddNoiseOptions{}.clip, "noise defaults");
        ctx.run_and_wait([&](Commands& c) {
            c.noise(source, {.seed = 123, .monochrome = false});
            c.radial_blur(source, destination,
                          test::reserve_workspace(ctx, source, RadialBlurOptions{.amount = 10},
                                                  radial_blur_requirements));
            c.radial_blur(
                source, scratch,
                test::reserve_workspace(ctx, source,
                                        RadialBlurOptions{.center = {18.5f, 14.5f}, .amount = 10},
                                        radial_blur_requirements));
        });
        auto a = get(ctx, destination), b = get(ctx, scratch);
        for (std::size_t i = 0; i < a.size(); ++i) {
            near(a[i], b[i]);
        }
        ctx.run_and_wait([&](Commands& c) {
            c.fill(destination, {.color = {0, .25f, .5f, .5f}});
            c.add_noise(destination, {.amount = 400, .clip = true});
        });
        bool varied = false;
        for (auto p : get(ctx, destination)) {
            test::near(p[3], .5f);
            for (int channel = 0; channel < 3; ++channel) {
                test::check(p[channel] >= 0 && p[channel] <= .5f,
                            "default noise exceeded display range");
            }
            varied |= p[0] != p[1] || p[1] != p[2];
        }
        test::check(varied, "default noise should have independent channels");
    });
    return test::finish();
}
