#include "../test.h"
#include <array>
#include <limits>

using namespace wgpupixel;
namespace {
using RGB = std::array<double, 3>;
using Pixel = std::array<float, 4>;
double clamp(double v) {
    return std::clamp(v, 0.0, 1.0);
}
double encode(double v) {
    v = clamp(v);
    return v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1 / 2.4) - 0.055;
}
double decode(double v) {
    v = clamp(v);
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}
double luma(RGB c) {
    return .2126 * c[0] + .7152 * c[1] + .0722 * c[2];
}
RGB hsl(RGB c) {
    const double lo = std::ranges::min(c), hi = std::ranges::max(c), d = hi - lo;
    const double l = (hi + lo) / 2;
    if (d == 0) {
        return {0, 0, l};
    }
    double h = hi == c[0]   ? (c[1] - c[2]) / d
               : hi == c[1] ? (c[2] - c[0]) / d + 2
                            : (c[0] - c[1]) / d + 4;
    h /= 6;
    h -= std::floor(h);
    return {h, d / (1 - std::abs(2 * l - 1)), l};
}
RGB rgb(RGB h) {
    const double chroma = (1 - std::abs(2 * h[2] - 1)) * h[1];
    double sector = (h[0] - std::floor(h[0])) * 6;
    const double x = chroma * (1 - std::abs(std::fmod(sector, 2) - 1));
    RGB c;
    switch (int(sector)) {
    case 0:
        c = {chroma, x, 0};
        break;
    case 1:
        c = {x, chroma, 0};
        break;
    case 2:
        c = {0, chroma, x};
        break;
    case 3:
        c = {0, x, chroma};
        break;
    case 4:
        c = {x, 0, chroma};
        break;
    default:
        c = {chroma, 0, x};
        break;
    }
    for (auto& v : c) {
        v += h[2] - chroma / 2;
    }
    return c;
}
RGB preserve(RGB c, RGB source) {
    const double target = luma(source), shift = target - luma(c);
    const double low = std::min(0.0, std::ranges::min(source));
    const double high = std::max(1.0, std::ranges::max(source));
    for (auto& v : c) {
        v += shift;
    }
    // Independently search the largest in-gamut point on the gray-to-color segment.
    double left = 0, right = 1;
    for (int i = 0; i < 60; ++i) {
        const double scale = (left + right) / 2;
        const bool inside = std::ranges::all_of(c, [&](double v) {
            const double value = target + (v - target) * scale;
            return value >= low && value <= high;
        });
        (inside ? left : right) = scale;
    }
    for (auto& v : c) {
        v = target + (v - target) * left;
    }
    return c;
}
double encode_extended(double v) {
    return std::copysign(std::abs(v) <= .0031308 ? std::abs(v) * 12.92
                                                 : 1.055 * std::pow(std::abs(v), 1 / 2.4) - .055,
                         v);
}
double decode_extended(double v) {
    return std::copysign(std::abs(v) <= .04045 ? std::abs(v) / 12.92
                                               : std::pow((std::abs(v) + .055) / 1.055, 2.4),
                         v);
}
template <class F> Pixel display(Pixel p, F f, bool bounded = false) {
    if (p[3] == 0) {
        return {};
    }
    RGB c{};
    for (int i = 0; i < 3; ++i) {
        c[i] = bounded ? encode(double(p[i]) / p[3]) : encode_extended(double(p[i]) / p[3]);
    }
    c = f(c);
    for (int i = 0; i < 3; ++i) {
        p[i] = float((bounded ? decode(c[i]) : decode_extended(c[i])) * p[3]);
    }
    return p;
}
template <class T> auto bytes(std::vector<T>& v) {
    return std::span(reinterpret_cast<std::uint8_t*>(v.data()), v.size() * sizeof(T));
}
template <class Apply, class Reference>
void compare(Context& ctx, Apply apply, Reference reference, float tolerance = 3e-5f) {
    auto image = ctx.create_image({17, 3});
    auto mask = ctx.create_mask(image.size());
    auto upload = ctx.create_upload_buffer(image, {.format = TransferFormat::rgba32_float});
    auto download = ctx.create_readback_buffer(image, {.format = TransferFormat::rgba32_float});
    auto coverage = ctx.create_upload_buffer(mask);
    std::vector<Pixel> input(51), output(51);
    std::vector<std::uint8_t> masks(51);
    for (int i = 0; i < 51; ++i) {
        const float alpha = i % 7 == 0 ? 0 : i % 7 == 1 ? 1 : .37f;
        for (int c = 0; c < 3; ++c) {
            input[i][c] = alpha * float((i * 29 + c * 47) % 101) / 100;
        }
        input[i][3] = alpha;
        masks[i] = std::array<std::uint8_t, 4>{0, 64, 128, 255}[i % 4];
    }
    input[2] = {0, 0, 0, 1};
    input[3] = {1, 1, 1, 1};
    input[4] = {1, 0, 0, 1};
    input[5] = {0, 1, 0, 1};
    input[6] = {0, 0, 1, 1};
    input[8] = {-.1f, .7f, 1.4f, .5f};
    input[9] = {1e-20f, 2e-20f, 3e-20f, 4e-20f};
    input[10] = {.2f, .2f, .2f, .5f};
    ctx.write(upload, bytes(input));
    ctx.write(coverage, masks);
    for (int mode = 0; mode < 6; ++mode) {
        const bool selected = mode % 2;
        const std::optional<Rect> region = mode == 4   ? std::optional<Rect>{{40, 10, 5, 5}}
                                           : mode == 5 ? std::optional<Rect>{{1, 1, 0, 0}}
                                           : mode >= 2 ? std::optional<Rect>{{-1, 1, 14, 4}}
                                                       : std::nullopt;
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.upload(upload, image);
            cmd.upload(coverage, mask);
            apply(cmd, image, selected ? &mask : nullptr, region);
            cmd.download(image, download);
        });
        ctx.read(download, bytes(output));
        for (int i = 0; i < 51; ++i) {
            const auto expected = reference(input[i], i);
            const float weight =
                region && (i / 17 < region->y || i / 17 >= region->y + region->height ||
                           i % 17 < region->x || i % 17 >= region->x + region->width)
                    ? 0
                : selected ? masks[i] / 255.f
                           : 1;
            for (int c = 0; c < 4; ++c) {
                test::near(output[i][c], input[i][c] + weight * (expected[c] - input[i][c]),
                           tolerance);
            }
        }
    }
    ctx.destroy(coverage);
    ctx.destroy(download);
    ctx.destroy(upload);
    ctx.destroy(mask);
    ctx.destroy(image);
}
double level(double x, LevelsTransfer p) {
    const auto normalized = [&](double value) {
        return (value - p.input_black) / (p.input_white - p.input_black);
    };
    const double edge = clamp(x);
    double value = std::pow(clamp(normalized(edge)), 1.0 / p.gamma);
    if (x != edge) {
        const auto power = [&](double v) {
            return std::copysign(std::pow(std::abs(v), 1.0 / p.gamma), v);
        };
        value += power(normalized(x)) - power(normalized(edge));
    }
    return p.output_black + (p.output_white - p.output_black) * value;
}
} // namespace
int main() {
    auto ctx = Context::create();
    test::run("levels channel order, gamma, output reversal, coverage and clipping", [&] {
        LevelsOptions o{.composite = {.input_black = .1f,
                                      .input_white = .9f,
                                      .gamma = 1.3f,
                                      .output_black = .8f,
                                      .output_white = .2f},
                        .red = {.gamma = .7f},
                        .green = {.input_black = .15f},
                        .blue = {.output_white = .75f}};
        compare(
            ctx,
            [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                o.mask = m;
                o.region = r;
                cmd.levels(image, o);
            },
            [&](Pixel p, int) {
                return display(p, [&](RGB c) {
                    const std::array channels{o.red, o.green, o.blue};
                    for (int i = 0; i < 3; ++i) {
                        c[i] = level(level(c[i], channels[i]), o.composite);
                    }
                    return c;
                });
            });
    });
    test::run("curves PCHIP analytical parabola and constant SDR endpoints", [&] {
        // PCHIP slopes of (0,0),(.5,1),(1,0) are 4,0,-4: exactly 4x(1-x).
        const std::array points{Point{0, 0}, Point{.5f, 1}, Point{1, 0}};
        const std::array red{Point{.2f, .1f}, Point{.8f, .9f}};
        compare(
            ctx,
            [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                cmd.curves(image, {.composite = points, .red = red, .mask = m, .region = r});
            },
            [](Pixel p, int) {
                return display(p, [](RGB c) {
                    c[0] = c[0] < 0   ? .1 + c[0] * (.8 / .6)
                           : c[0] > 1 ? .9 + (c[0] - 1) * (.8 / .6)
                                      : .1 + .8 * clamp((c[0] - .2) / .6);
                    for (auto& v : c) {
                        v = v < 0 ? 4 * v : v > 1 ? -4 * (v - 1) : 4 * v * (1 - v);
                    }
                    return c;
                });
            },
            2e-4f);
    });
    test::run("curves uneven knots use weighted harmonic tangents", [&] {
        const std::array points{Point{0, 0}, Point{0.25f, 0.2f}, Point{0.75f, 0.8f}, Point{1, 1}};
        compare(
            ctx,
            [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                cmd.curves(image, {.composite = points, .mask = m, .region = r});
            },
            [](Pixel p, int) {
                return display(p, [](RGB c) {
                    const std::array<double, 4> x{0, .25, .75, 1}, y{0, .2, .8, 1};
                    const std::array<double, 4> slope{2.0 / 3, 108.0 / 115, 108.0 / 115, 2.0 / 3};
                    for (auto& value : c) {
                        if (value < 0) {
                            value *= 2.0 / 3;
                            continue;
                        }
                        if (value > 1) {
                            value = 1 + (value - 1) * 2.0 / 3;
                            continue;
                        }
                        const int i = value < .25 ? 0 : value < .75 ? 1 : 2;
                        const double h = x[i + 1] - x[i], t = (value - x[i]) / h;
                        const double a = y[i], b = h * slope[i];
                        const double d = 2 * y[i] - 2 * y[i + 1] + h * (slope[i] + slope[i + 1]);
                        const double e =
                            -3 * y[i] + 3 * y[i + 1] - h * (2 * slope[i] + slope[i + 1]);
                        value = a + t * (b + t * (e + t * d));
                    }
                    return c;
                });
            });
    });
    test::run("raw LUT interpolation, channel order and domains", [&] {
        const std::array<float, 3> composite{.1f, .8f, .9f};
        const std::array<float, 2> red{1, 0};
        for (auto domain : {ColorEncoding::srgb, ColorEncoding::linear}) {
            compare(
                ctx,
                [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                    cmd.lut(image, {.composite = composite,
                                    .red = red,
                                    .domain = domain,
                                    .mask = m,
                                    .region = r});
                },
                [&](Pixel p, int) {
                    if (p[3] == 0) {
                        return Pixel{};
                    }
                    for (int i = 0; i < 3; ++i) {
                        double x = double(p[i]) / p[3];
                        if (domain == ColorEncoding::srgb) {
                            x = encode_extended(x);
                        }
                        if (i == 0) {
                            x = 1 - x;
                        }
                        x = x <= .5 ? .1 + 1.4 * x : .8 + .2 * (x - .5);
                        p[i] =
                            float((domain == ColorEncoding::srgb ? decode_extended(x) : x) * p[3]);
                    }
                    return p;
                });
        }
    });
    test::run("3D LUT red-fastest trilinear cross terms, boundaries and domains", [&] {
        for (unsigned n : {2u, 5u, 65u}) {
            for (auto domain : {ColorEncoding::srgb, ColorEncoding::linear}) {
                std::vector<float> values;
                for (unsigned b = 0; b < n; ++b) {
                    for (unsigned g = 0; g < n; ++g) {
                        for (unsigned r = 0; r < n; ++r) {
                            const float x = float(r) / (n - 1), y = float(g) / (n - 1),
                                        z = float(b) / (n - 1);
                            values.insert(values.end(), {x * y, y * z, z * x});
                        }
                    }
                }
                compare(
                    ctx,
                    [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                        cmd.lut3d(image, {.size = n,
                                          .values = values,
                                          .domain = domain,
                                          .mask = m,
                                          .region = r});
                    },
                    [&](Pixel p, int) {
                        if (p[3] == 0) {
                            return Pixel{};
                        }
                        RGB c{};
                        for (int i = 0; i < 3; ++i) {
                            c[i] = domain == ColorEncoding::srgb ? encode(double(p[i]) / p[3])
                                                                 : clamp(double(p[i]) / p[3]);
                        }
                        c = {c[0] * c[1], c[1] * c[2], c[2] * c[0]};
                        for (int i = 0; i < 3; ++i) {
                            p[i] =
                                float((domain == ColorEncoding::srgb ? decode(c[i]) : c[i]) * p[3]);
                        }
                        return p;
                    });
            }
        }
    });
    test::run("color balance tonal weights and luma preservation", [&] {
        for (bool keep : {false, true}) {
            ColorBalanceOptions o{.shadows = {.2f, -.3f, .1f},
                                  .midtones = {-.1f, .2f, .3f},
                                  .highlights = {.4f, -.1f, -.2f},
                                  .preserve_luminosity = keep};
            compare(
                ctx,
                [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                    o.mask = m;
                    o.region = r;
                    cmd.color_balance(image, o);
                },
                [&](Pixel p, int) {
                    return display(p, [&](RGB c) {
                        const RGB source = c;
                        const double l = hsl(c)[2];
                        const double sh = 1 - clamp((l - (1.0 / 3 - .125)) * 4);
                        const double hi = clamp((l - (2.0 / 3 - .125)) * 4);
                        for (int i = 0; i < 3; ++i) {
                            c[i] += .7 * (o.shadows[i] * sh + o.midtones[i] * (1 - sh - hi) +
                                          o.highlights[i] * hi);
                        }
                        return keep ? preserve(c, source) : c;
                    });
                });
        }
    });
    test::run("hue saturation HSL shifts, colorize and lightness extremes", [&] {
        for (bool colorize : {false, true}) {
            for (float amount : {-1.f, 0.f, .4f, 1.f}) {
                HueSaturationOptions o{.hue = -135,
                                       .saturation = colorize ? .65f : amount,
                                       .lightness = amount,
                                       .colorize = colorize};
                compare(
                    ctx,
                    [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                        o.mask = m;
                        o.region = r;
                        cmd.hue_saturation(image, o);
                    },
                    [&](Pixel p, int) {
                        return display(p, [&](RGB c) {
                            const double low = std::min(0.0, std::ranges::min(c));
                            const double width = std::max(1.0, std::ranges::max(c)) - low;
                            for (auto& v : c) {
                                v = (v - low) / width;
                            }
                            auto h = hsl(c);
                            h[0] = colorize ? -.375 : h[0] - .375;
                            h[1] = colorize ? .65 : std::min(1.0, h[1] * (1 + amount));
                            h[2] = amount < 0 ? h[2] * (1 + amount) : h[2] + (1 - h[2]) * amount;
                            auto result = rgb(h);
                            for (auto& v : result) {
                                v = v * width + low;
                            }
                            return result;
                        });
                    });
            }
        }
    });
    test::run("color matrix straight RGBA, offsets and alpha creation", [&] {
        ColorMatrixOptions o{.matrix = {.8f, .2f, 0,   0,   .1f, 0, 1, 0, 0,   -.1f,
                                        .1f, 0,   .7f, .1f, 0,   0, 0, 0, .5f, .2f}};
        for (auto domain : {ColorEncoding::linear, ColorEncoding::srgb}) {
            o.domain = domain;
            compare(
                ctx,
                [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                    o.mask = m;
                    o.region = r;
                    cmd.color_matrix(image, o);
                },
                [&](Pixel p, int) {
                    std::array<double, 5> v{0, 0, 0, p[3], 1};
                    for (int i = 0; i < 3; ++i) {
                        v[i] = p[3] > 0 ? double(p[i]) / p[3] : 0;
                        if (domain == ColorEncoding::srgb) {
                            v[i] = encode_extended(v[i]);
                        }
                    }
                    Pixel result{};
                    for (int row = 0; row < 4; ++row) {
                        for (int col = 0; col < 5; ++col) {
                            result[row] += float(o.matrix[row * 5 + col] * v[col]);
                        }
                    }
                    result[3] = float(clamp(result[3]));
                    for (int i = 0; i < 3; ++i) {
                        result[i] =
                            float((domain == ColorEncoding::srgb ? decode_extended(result[i])
                                                                 : result[i]) *
                                  result[3]);
                    }
                    return result;
                });
        }
    });
    test::run("channel mixer coefficients constants and monochrome", [&] {
        for (bool mono : {false, true}) {
            ChannelMixerOptions o{.red = {.5f, .3f, .2f, .1f},
                                  .green = {0, 1.2f, -.1f, 0},
                                  .blue = {.2f, .1f, .7f, -.1f},
                                  .monochrome = mono};
            compare(
                ctx,
                [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                    o.mask = m;
                    o.region = r;
                    cmd.channel_mixer(image, o);
                },
                [&](Pixel p, int) {
                    return display(p, [&](RGB c) {
                        RGB result{};
                        const std::array rows{o.red, o.green, o.blue};
                        for (int i = 0; i < 3; ++i) {
                            const auto row = rows[mono ? 0 : i];
                            result[i] = row[0] * c[0] + row[1] * c[1] + row[2] * c[2] + row[3];
                        }
                        return result;
                    });
                });
        }
    });
    test::run("black white hue weights and optional tint", [&] {
        for (bool tint : {false, true}) {
            BlackWhiteOptions o{.weights = {.6f, .8f, .3f, .5f, .1f, .7f}, .tint = tint};
            compare(
                ctx,
                [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                    o.mask = m;
                    o.region = r;
                    cmd.black_white(image, o);
                },
                [&](Pixel p, int) {
                    return display(p, [&](RGB c) {
                        const double sector = hsl(c)[0] * 6;
                        const int i = int(sector) % 6;
                        const double weight =
                            o.weights[i] +
                            (o.weights[(i + 1) % 6] - o.weights[i]) * (sector - std::floor(sector));
                        const double gray = std::ranges::min(c) +
                                            (std::ranges::max(c) - std::ranges::min(c)) * weight;
                        if (!tint) {
                            return RGB{gray, gray, gray};
                        }
                        auto h = hsl({encode(o.tint_color.r), encode(o.tint_color.g),
                                      encode(o.tint_color.b)});
                        const double low = std::min(0.0, gray), width = std::max(1.0, gray) - low;
                        h[2] = (gray - low) / width;
                        auto result = rgb(h);
                        for (auto& v : result) {
                            v = v * width + low;
                        }
                        return result;
                    });
                });
        }
    });
    test::run("photo filter multiplicative density and preserved luma", [&] {
        for (bool keep : {false, true}) {
            for (float density : {0.f, .7f, 1.f}) {
                PhotoFilterOptions o{
                    .color = {.8f, .2f, .05f, 1}, .density = density, .preserve_luminosity = keep};
                compare(
                    ctx,
                    [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                        o.mask = m;
                        o.region = r;
                        cmd.photo_filter(image, o);
                    },
                    [&](Pixel p, int) {
                        if (density == 0) {
                            return p;
                        }
                        return display(p, [&](RGB c) {
                            const RGB source = c;
                            const RGB filter{encode(o.color.r), encode(o.color.g),
                                             encode(o.color.b)};
                            for (int i = 0; i < 3; ++i) {
                                c[i] *= 1 - density + filter[i] * density;
                            }
                            return keep ? preserve(c, source) : c;
                        });
                    });
            }
        }
    });
    test::run("posterize encoded quantization", [&] {
        for (unsigned levels : {2u, 7u, 256u}) {
            compare(
                ctx,
                [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                    cmd.posterize(image, {.levels = levels, .mask = m, .region = r});
                },
                [&](Pixel p, int) {
                    return display(
                        p,
                        [&](RGB c) {
                            for (auto& v : c) {
                                v = std::min(double(levels - 1), std::floor(v * levels)) /
                                    (levels - 1);
                            }
                            return c;
                        },
                        true);
                });
        }
    });
    test::run("gradient map stop interpolation alpha reverse and deterministic dither", [&] {
        const std::array stops{GradientStop{.1f, {.1f, 0, .2f, .3f}},
                               GradientStop{.5f, {.2f, .3f, .1f, .6f}},
                               GradientStop{.9f, {.8f, .9f, .7f, 1}}};
        for (bool reverse : {false, true}) {
            for (bool dither : {false, true}) {
                compare(
                    ctx,
                    [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                        cmd.gradient_map(image, {.stops = stops,
                                                 .reverse = reverse,
                                                 .dither = dither,
                                                 .mask = m,
                                                 .region = r});
                    },
                    [&](Pixel p, int index) {
                        if (p[3] == 0) {
                            return Pixel{};
                        }
                        double x = luma({encode(double(p[0]) / p[3]), encode(double(p[1]) / p[3]),
                                         encode(double(p[2]) / p[3])});
                        if (reverse) {
                            x = 1 - x;
                        }
                        if (dither) {
                            std::uint32_t h = std::uint32_t(index % 17) * 1664525u +
                                              std::uint32_t(index / 17) * 1013904223u + 2246822519u;
                            h = (h ^ (h >> 16)) * 2246822519u;
                            x += (double(h & 65535) / 65535 - .5) / 255;
                        }
                        const auto a = stops[x < .5 ? 0 : 1], b = stops[x < .5 ? 1 : 2];
                        const double t = clamp((x - a.position) / (b.position - a.position));
                        const Pixel ca{a.color.r, a.color.g, a.color.b, a.color.a},
                            cb{b.color.r, b.color.g, b.color.b, b.color.a};
                        Pixel result{};
                        const double alpha = ca[3] + t * (cb[3] - ca[3]);
                        for (int i = 0; i < 3; ++i) {
                            const double ea = encode(ca[i] / ca[3]) * ca[3];
                            const double eb = encode(cb[i] / cb[3]) * cb[3];
                            result[i] = float(decode((ea + t * (eb - ea)) / alpha) * alpha * p[3]);
                        }
                        result[3] = float(alpha * p[3]);
                        return result;
                    });
            }
        }
    });
    test::run("curves monotonic plateaus and default adjustment identities", [&] {
        const std::array points{Point{0, 0.25f}, Point{0.2f, 0.25f}, Point{0.8f, 0.75f},
                                Point{1, 0.75f}};
        compare(
            ctx,
            [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                cmd.curves(image, {.composite = points, .mask = m, .region = r});
            },
            [](Pixel p, int) {
                return display(p, [](RGB c) {
                    for (auto& v : c) {
                        const double t = clamp((v - 0.2) / 0.6);
                        v = 0.25 + 0.5 * t * t * (3 - 2 * t);
                    }
                    return c;
                });
            });
        compare(
            ctx,
            [](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                cmd.lut(image, {.mask = m, .region = r});
            },
            [](Pixel p, int) { return p; });
    });
    test::run("recorded tables own data and failed capacity can be retried", [&] {
        auto image = ctx.create_image({1, 1});
        auto cmd = ctx.create_commands(1);
        cmd.fill(image, {.color = {0.2f, 0.1f, 0.3f, 0.5f}});
        const std::array<float, 2> table{0.5f, 0.5f};
        ctx.set_memory_limit(ctx.memory().total);
        test::error(ErrorCode::capacity, "lut", "memory_limit",
                    [&] { cmd.lut(image, {.composite = table}); });
        ctx.set_memory_limit(0);
        ctx.submit_and_wait(cmd);
        {
            std::vector<float> temporary{0.5f, 0.5f};
            cmd.lut(image, {.composite = temporary});
            temporary.assign(2, 0);
        }
        ctx.submit_and_wait(cmd);
        const auto result = test::read(ctx, image);
        for (int c = 0; c < 3; ++c) {
            test::check(std::abs(int(result[c]) - 128) <= 1, "table was not copied");
        }
        ctx.destroy(image);
    });
    test::run("adjustment validation is atomic and names parameters", [&] {
        auto image = ctx.create_image({1, 1});
        auto cmd = ctx.create_commands(1);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        test::error(ErrorCode::invalid_argument, "levels", "composite.gamma",
                    [&] { cmd.levels(image, {.composite = {.gamma = 0}}); });
        const std::array points{Point{.5f, 0}, Point{.5f, 1}};
        test::error(ErrorCode::invalid_argument, "curves", "red",
                    [&] { cmd.curves(image, {.red = points}); });
        const std::array table{0.f, nan};
        test::error(ErrorCode::invalid_argument, "lut", "blue",
                    [&] { cmd.lut(image, {.blue = table}); });
        test::error(ErrorCode::invalid_argument, "lut3d", "size",
                    [&] { cmd.lut3d(image, {.size = 66}); });
        test::error(ErrorCode::invalid_argument, "lut3d", "values", [&] { cmd.lut3d(image, {}); });
        test::error(ErrorCode::invalid_argument, "color_balance", "shadows",
                    [&] { cmd.color_balance(image, {.shadows = {nan, 0, 0}}); });
        test::error(ErrorCode::invalid_argument, "hue_saturation", "saturation",
                    [&] { cmd.hue_saturation(image, {.saturation = -.5f, .colorize = true}); });
        ColorMatrixOptions matrix;
        matrix.matrix[0] = nan;
        test::error(ErrorCode::invalid_argument, "color_matrix", "matrix",
                    [&] { cmd.color_matrix(image, matrix); });
        test::error(ErrorCode::invalid_argument, "channel_mixer", "green",
                    [&] { cmd.channel_mixer(image, {.green = {3, 0, 0, 0}}); });
        test::error(ErrorCode::invalid_argument, "black_white", "weights",
                    [&] { cmd.black_white(image, {.weights = {4, 0, 0, 0, 0, 0}}); });
        test::error(ErrorCode::invalid_argument, "photo_filter", "color",
                    [&] { cmd.photo_filter(image, {.color = {1, 1, 1, .5f}}); });
        test::error(ErrorCode::invalid_argument, "posterize", "levels",
                    [&] { cmd.posterize(image, {.levels = 1}); });
        test::error(ErrorCode::invalid_argument, "gradient_map", "stops",
                    [&] { cmd.gradient_map(image, {}); });
        cmd.fill(image, {.color = {0, 0, 0, 1}});
        ctx.submit_and_wait(cmd);
        ctx.destroy(image);
    });
    return test::finish();
}
