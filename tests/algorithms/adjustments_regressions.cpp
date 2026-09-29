#include "../test.h"
#include <array>
#include <bit>
#include <limits>

using namespace wgpupixel;
namespace {
using Pixel = std::array<float, 4>;
float linear(double encoded) {
    const double v = std::abs(encoded);
    return float(
        std::copysign(v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4), encoded));
}
Pixel encoded(double r, double g, double b) {
    return {linear(r), linear(g), linear(b), 1};
}
Pixel gray(double value) {
    return encoded(value, value, value);
}
template <class Apply>
void check(Context& ctx, std::span<const Pixel> inputs, std::span<const Pixel> outputs, Apply apply,
           bool exact = false, float tolerance = 3e-6f) {
    constexpr int width = 37, height = 19;
    auto image = ctx.create_image({width, height});
    auto mask = ctx.create_mask(image.size());
    auto upload = ctx.create_upload_buffer(image, {.format = TransferFormat::rgba32_float});
    auto download = ctx.create_readback_buffer(image, {.format = TransferFormat::rgba32_float});
    auto mask_upload = ctx.create_upload_buffer(mask);
    std::vector<Pixel> before(width * height), after(before.size()), expected(before.size());
    std::vector<std::uint8_t> coverage(before.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        before[i] = inputs[i % inputs.size()];
        expected[i] = outputs[i % outputs.size()];
        const float alpha = std::array{1.f, .375f, 0.f}[(i / inputs.size()) % 3];
        for (int c = 0; c < 4; ++c) {
            before[i][c] *= alpha;
            expected[i][c] *= alpha;
        }
        coverage[i] = std::array<std::uint8_t, 3>{0, 128, 255}[(i / 3) % 3];
    }
    auto bytes = [](auto& v) {
        return std::span(reinterpret_cast<std::uint8_t*>(v.data()), v.size() * sizeof(v[0]));
    };
    ctx.write(upload, bytes(before));
    ctx.write(mask_upload, coverage);
    for (int mode = 0; mode < 4; ++mode) {
        const bool masked = mode & 1;
        const std::optional<Rect> region =
            mode & 2 ? std::optional<Rect>{{3, 2, 29, 13}} : std::nullopt;
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.upload(upload, image);
            cmd.upload(mask_upload, mask);
            apply(cmd, image, masked ? &mask : nullptr, region);
            cmd.download(image, download);
        });
        ctx.read(download, bytes(after));
        for (int i = 0; i < width * height; ++i) {
            const bool outside =
                region && (i % width < 3 || i % width >= 32 || i / width < 2 || i / width >= 15);
            const float weight = outside ? 0 : masked ? coverage[i] / 255.f : 1;
            for (int c = 0; c < 4; ++c) {
                if (exact) {
                    test::check(std::bit_cast<std::uint32_t>(after[i][c]) ==
                                    std::bit_cast<std::uint32_t>(before[i][c]),
                                "neutral control changed storage bits");
                } else {
                    test::near(after[i][c], before[i][c] + weight * (expected[i][c] - before[i][c]),
                               tolerance);
                }
            }
        }
    }
    ctx.destroy(mask_upload);
    ctx.destroy(download);
    ctx.destroy(upload);
    ctx.destroy(mask);
    ctx.destroy(image);
}
} // namespace
int main() {
    auto ctx = Context::create();
    test::run("near-gray saturation has bounded chroma and no neutral discontinuity", [&] {
        const std::array inputs{encoded(.5, .5005, .5), gray(.5)};
        // At fixed lightness .50025, 1.5x/2x the .0005 chroma gives these extrema.
        const std::array half{encoded(.499875, .500625, .499875), gray(.5)};
        const std::array full{encoded(.49975, .50075, .49975), gray(.5)};
        for (float s : {.5f, 1.f}) {
            check(ctx, inputs, s == .5f ? half : full,
                  [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                      cmd.hue_saturation(image, {.saturation = s, .mask = m, .region = r});
                  });
        }
    });
    test::run("black-white gradient maps are gray identities and encoded color midpoints", [&] {
        const std::array stops{GradientStop{0, {0, 0, 0, 1}}, GradientStop{1, {1, 1, 1, 1}}};
        std::vector<Pixel> ramp;
        for (int i = 0; i <= 256; ++i) {
            ramp.push_back(gray(double(i) / 256));
        }
        check(ctx, ramp, ramp,
              [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                  cmd.gradient_map(image, {.stops = stops, .mask = m, .region = r});
              });
        const std::array colors{GradientStop{0, {1, 0, 0, 1}}, GradientStop{1, {0, 0, 1, 1}}};
        const std::array<Pixel, 1> input{gray(.5)}, output{encoded(.5, 0, .5)};
        check(ctx, input, output,
              [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                  cmd.gradient_map(image, {.stops = colors, .mask = m, .region = r});
              });
    });
    test::run("duplicate gradient stops have exact right-continuous hard edges", [&] {
        // Values on either side detect interpolation across the discontinuity.
        const std::array stops{GradientStop{0, {1, 0, 0, 1}}, GradientStop{.5f, {1, 0, 0, 1}},
                               GradientStop{.5f, {0, 0, 1, 1}}, GradientStop{1, {0, 0, 1, 1}}};
        const std::array input{gray(.499), gray(.5), gray(.501)};
        const std::array output{encoded(1, 0, 0), encoded(0, 0, 1), encoded(0, 0, 1)};
        check(ctx, input, output,
              [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                  cmd.gradient_map(image, {.stops = stops, .mask = m, .region = r});
              });
        const std::array reversed{encoded(0, 0, 1), encoded(0, 0, 1), encoded(1, 0, 0)};
        check(ctx, input, reversed,
              [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                  cmd.gradient_map(image,
                                   {.stops = stops, .reverse = true, .mask = m, .region = r});
              });
        // A power-of-two linear value remains exact after premultiplication by 3/8.
        // In the sRGB linear toe its boundary coordinate is exactly 12.92 / 1024.
        constexpr float boundary = 12.92f / 1024;
        const std::array exact_stops{
            GradientStop{0, {1, 0, 0, 1}}, GradientStop{boundary, {1, 0, 0, 1}},
            GradientStop{boundary, {0, 0, 1, 1}}, GradientStop{1, {0, 0, 1, 1}}};
        const std::array exact_input{Pixel{1.f / 2048, 1.f / 2048, 1.f / 2048, 1},
                                     Pixel{1.f / 1024, 1.f / 1024, 1.f / 1024, 1},
                                     Pixel{1.f / 512, 1.f / 512, 1.f / 512, 1}};
        check(ctx, exact_input, output,
              [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                  cmd.gradient_map(image, {.stops = exact_stops, .mask = m, .region = r});
              });
        for (float position : {0.f, 1.f}) {
            const std::array coincident{GradientStop{position, {1, 0, 0, 1}},
                                        GradientStop{position, {0, 0, 1, 1}}};
            const std::array ends{gray(0), gray(1), Pixel{-1, -1, -1, 1}, Pixel{2, 2, 2, 1}};
            const auto low = position == 0 ? encoded(0, 0, 1) : encoded(1, 0, 0);
            const std::array expected{low, encoded(0, 0, 1), low, encoded(0, 0, 1)};
            check(ctx, ends, expected,
                  [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                      cmd.gradient_map(image, {.stops = coincident, .mask = m, .region = r});
                  });
        }
        std::vector<GradientStop> maximum(4096, GradientStop{.5f, {1, 0, 0, 1}});
        maximum.back().color = {0, 0, 1, 1};
        check(ctx, input, output,
              [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                  cmd.gradient_map(image, {.stops = maximum, .mask = m, .region = r});
              });
    });
    test::run("3D LUT cube domains reproduce signed HDR coordinates", [&] {
        const std::array<float, 3> minimum{-1, -2, 0}, maximum{3, 2, 4};
        std::vector<float> values;
        // An affine function is reproduced exactly by trilinear interpolation,
        // independent of how the implementation locates/interpolates its cells.
        for (int b = 0; b < 2; ++b) {
            for (int g = 0; g < 2; ++g) {
                for (int r = 0; r < 2; ++r) {
                    values.insert(values.end(), {r ? 3.f : -1.f, g ? 2.f : -2.f, b ? 4.f : 0.f});
                }
            }
        }
        const std::array input{Pixel{1.5f, -1, 3, 1}, Pixel{-2, 3, 5, 1},
                               Pixel{.25f, .5f, 1.5f, 1}};
        const std::array output{input[0], Pixel{-1, 2, 4, 1}, input[2]};
        check(ctx, input, output,
              [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                  cmd.lut3d(image, {.values = values,
                                    .domain = ColorEncoding::linear,
                                    .domain_min = minimum,
                                    .domain_max = maximum,
                                    .mask = m,
                                    .region = r});
              });
        // Constant cubes give a known result at every coordinate, including out-of-range entries.
        for (auto domain : {ColorEncoding::srgb, ColorEncoding::linear}) {
            const Pixel constant{1.0000001f, -1e-6f, 1.5f, 1};
            values.clear();
            for (int i = 0; i < 8; ++i) {
                values.insert(values.end(), constant.begin(), constant.begin() + 3);
            }
            const std::array<Pixel, 1> result{domain == ColorEncoding::linear
                                                  ? constant
                                                  : encoded(constant[0], constant[1], constant[2])};
            check(ctx, input, result,
                  [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                      cmd.lut3d(image,
                                {.values = values, .domain = domain, .mask = m, .region = r});
                  });
        }
    });
    test::run("neutral adjustment layers preserve signed HDR storage exactly", [&] {
        const std::array input{Pixel{1.4f, .5f, -.1f, 1}, Pixel{-.2f, 8, .003f, 1},
                               Pixel{0, 0, 0, 1}};
        const std::array<float, 3> table{0, .5f, 1};
        const std::array points{Point{0, 0}, Point{.3f, .3f}, Point{1, 1}};
        for (int operation = 0; operation < 12; ++operation) {
            check(
                ctx, input, input,
                [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                    switch (operation) {
                    case 0:
                        cmd.levels(image, {.mask = m, .region = r});
                        break;
                    case 1:
                        cmd.curves(image, {.mask = m, .region = r});
                        break;
                    case 2:
                        cmd.curves(image, {.composite = points, .mask = m, .region = r});
                        break;
                    case 3:
                        cmd.lut(image, {.mask = m, .region = r});
                        break;
                    case 4:
                        cmd.lut(image, {.domain = ColorEncoding::linear, .mask = m, .region = r});
                        break;
                    case 5:
                        cmd.lut(image, {.composite = table, .red = table, .mask = m, .region = r});
                        break;
                    case 6:
                        cmd.hue_saturation(image, {.mask = m, .region = r});
                        break;
                    case 7:
                        cmd.color_balance(image, {.mask = m, .region = r});
                        break;
                    case 8:
                        cmd.color_matrix(image,
                                         {.domain = ColorEncoding::srgb, .mask = m, .region = r});
                        break;
                    case 9:
                        cmd.channel_mixer(image, {.mask = m, .region = r});
                        break;
                    case 10:
                        cmd.photo_filter(image, {.density = 0, .mask = m, .region = r});
                        break;
                    default:
                        cmd.photo_filter(image, {.color = {1, 1, 1, 1}, .mask = m, .region = r});
                        break;
                    }
                },
                true);
        }
    });
    test::run("color balance ranges isolate midgray and limit endpoint correction", [&] {
        const std::array input{gray(0), gray(.5), gray(1)};
        // Shadow correction must not reach midgray or white; full correction is .7.
        const std::array shadows{encoded(.7, 0, 0), input[1], input[2]};
        const std::array highlights{input[0], input[1], encoded(.3, 1, 1)};
        const std::array midtones{input[0], encoded(.85, .5, .5), input[2]};
        for (int band = 0; band < 3; ++band) {
            check(ctx, input,
                  band == 0   ? shadows
                  : band == 1 ? midtones
                              : highlights,
                  [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                      ColorBalanceOptions o{.preserve_luminosity = false, .mask = m, .region = r};
                      if (band == 0) {
                          o.shadows = {1, 0, 0};
                      }
                      if (band == 1) {
                          o.midtones = {.5f, 0, 0};
                      }
                      if (band == 2) {
                          o.highlights = {-1, 0, 0};
                      }
                      cmd.color_balance(image, o);
                  });
        }
    });
    test::run("posterize equal-width input bands", [&] {
        const std::array input{gray(.1),  gray(.2), gray(.3), gray(.45),
                               gray(.55), gray(.7), gray(.8), gray(.9)};
        const std::array output{gray(0),       gray(0),       gray(1.0 / 3), gray(1.0 / 3),
                                gray(2.0 / 3), gray(2.0 / 3), gray(1),       gray(1)};
        check(ctx, input, output,
              [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                  cmd.posterize(image, {.levels = 4, .mask = m, .region = r});
              });
    });
    test::run("colorize supports the full hue circle", [&] {
        const std::array<Pixel, 1> input{gray(.5)};
        for (float hue : {0.f, 180.f, 270.f, 360.f, -90.f}) {
            const std::array<Pixel, 1> output{hue == 180                 ? encoded(0, 1, 1)
                                              : hue == 270 || hue == -90 ? encoded(.5, 0, 1)
                                                                         : encoded(1, 0, 0)};
            check(ctx, input, output,
                  [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                      cmd.hue_saturation(
                          image,
                          {.hue = hue, .saturation = 1, .colorize = true, .mask = m, .region = r});
                  });
        }
    });
    test::run("SDR levels and curves match the four reviewed endpoint tables", [&] {
        const std::array input{gray(0), gray(.05), gray(.1), gray(.5), gray(.9), gray(1)};
        // Closed-form table rows, including the reviewer's exact black/white plateaus.
        const std::array levels_black{gray(.2),      gray(.2),        gray(.2),
                                      gray(5.0 / 9), gray(41.0 / 45), gray(1)};
        const std::array levels_white{gray(0),     gray(.05625), gray(.1125),
                                      gray(.5625), gray(.9),     gray(.9)};
        const std::array curves_black{gray(.3),    gray(.3),    gray(.3),
                                      gray(.5625), gray(.9125), gray(1)};
        const std::array curves_white{gray(0),    gray(.0625), gray(.125),
                                      gray(.625), gray(1),     gray(1)};
        const std::array rows{levels_black, levels_white, curves_black, curves_white};
        for (int operation = 0; operation < 4; ++operation) {
            check(ctx, input, rows[operation],
                  [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                      if (operation == 0) {
                          cmd.levels(image, {.composite = {.input_black = .1f, .output_black = .2f},
                                             .mask = m,
                                             .region = r});
                      } else if (operation == 1) {
                          cmd.levels(image, {.composite = {.input_white = .8f, .output_white = .9f},
                                             .mask = m,
                                             .region = r});
                      } else {
                          const std::array points = operation == 2
                                                        ? std::array{Point{.2f, .3f}, Point{1, 1}}
                                                        : std::array{Point{0, 0}, Point{.8f, 1}};
                          cmd.curves(image, {.composite = points, .mask = m, .region = r});
                      }
                  });
        }
    });
    test::run("HDR tails meet clipped SDR boundaries and converge as endpoints move", [&] {
        for (double e : {.001, .00001}) {
            const std::array input{gray(-e), gray(0), gray(e), gray(1 - e), gray(1), gray(1 + e)};
            const std::array levels_black{gray(.2 - e * 8 / 9), gray(.2), gray(.2),
                                          gray(1 - e * 8 / 9),  gray(1),  gray(1 + e * 8 / 9)};
            const std::array levels_white{gray(-e * 9 / 8), gray(0),  gray(e * 9 / 8),
                                          gray(.9),         gray(.9), gray(.9 + e * 9 / 8)};
            const std::array curves_black{gray(.3 - e * .875), gray(.3), gray(.3),
                                          gray(1 - e * .875),  gray(1),  gray(1 + e * .875)};
            const std::array curves_white{gray(-e * 1.25), gray(0), gray(e * 1.25),
                                          gray(1),         gray(1), gray(1 + e * 1.25)};
            const std::array rows{levels_black, levels_white, curves_black, curves_white};
            for (int operation = 0; operation < 4; ++operation) {
                check(ctx, input, rows[operation],
                      [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                          if (operation == 0) {
                              cmd.levels(image,
                                         {.composite = {.input_black = .1f, .output_black = .2f},
                                          .mask = m,
                                          .region = r});
                          } else if (operation == 1) {
                              cmd.levels(image,
                                         {.composite = {.input_white = .8f, .output_white = .9f},
                                          .mask = m,
                                          .region = r});
                          } else {
                              const std::array points =
                                  operation == 2 ? std::array{Point{.2f, .3f}, Point{1, 1}}
                                                 : std::array{Point{0, 0}, Point{.8f, 1}};
                              cmd.curves(image, {.composite = points, .mask = m, .region = r});
                          }
                      });
            }
            const std::array<Pixel, 1> hdr{Pixel{1.4f, .5f, -.1f, 1}};
            for (bool curves : {false, true}) {
                check(
                    ctx, hdr, hdr,
                    [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                        if (curves) {
                            const std::array points{Point{float(e), float(e)},
                                                    Point{float(1 - e), float(1 - e)}};
                            cmd.curves(image, {.composite = points, .mask = m, .region = r});
                        } else {
                            cmd.levels(image, {.composite = {.input_black = float(e),
                                                             .input_white = float(1 - e)},
                                               .mask = m,
                                               .region = r});
                        }
                    },
                    false, float(10 * e));
            }
        }
    });
    test::run("preserved luminosity fits SDR gamut without changing luma or hue", [&] {
        const std::array<Pixel, 1> input{gray(.5)};
        // At the red gamut faces, G=B. Rec.709 luma fixes the other coordinate:
        // .2126*R + .7874*G = .5. These are the gray-to-color ray intersections.
        for (float red : {-1.f, 1.f}) {
            const std::array<Pixel, 1> output{red > 0 ? encoded(1, .2874 / .7874, .2874 / .7874)
                                                      : encoded(0, .5 / .7874, .5 / .7874)};
            check(ctx, input, output,
                  [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                      cmd.color_balance(image, {.midtones = {red, 0, 0}, .mask = m, .region = r});
                  });
        }
        // Yellow has luma .9278; red filtering makes the same R=1, G=B gamut face.
        const std::array<Pixel, 1> yellow{encoded(1, 1, 0)},
            filtered{encoded(1, .7152 / .7874, .7152 / .7874)};
        check(ctx, yellow, filtered,
              [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                  cmd.photo_filter(image,
                                   {.color = {1, 0, 0, 1}, .density = 1, .mask = m, .region = r});
              });
    });
    test::run("active HDR controls converge continuously to neutral", [&] {
        // The reviewer's exact HDR pixel. Bounds shrink with the slider distance;
        // any clipping jump of .4 or .1 fails even the widest bound.
        const std::array<Pixel, 1> input{Pixel{1.4f, .5f, -.1f, 1}};
        for (float epsilon : {.001f, -.001f, .0001f, -.0001f, .00001f, -.00001f}) {
            for (int operation = 0; operation < 9; ++operation) {
                check(
                    ctx, input, input,
                    [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                        const std::array points{Point{0, 0}, Point{.5f, .5f + epsilon},
                                                Point{1, 1}};
                        const std::array table{epsilon, 1 + epsilon};
                        switch (operation) {
                        case 0:
                            cmd.levels(
                                image,
                                {.composite = {.gamma = 1 + epsilon}, .mask = m, .region = r});
                            break;
                        case 1:
                            cmd.curves(image, {.composite = points, .mask = m, .region = r});
                            break;
                        case 2:
                            cmd.hue_saturation(image,
                                               {.saturation = epsilon, .mask = m, .region = r});
                            break;
                        case 3:
                            cmd.lut(image, {.composite = table, .mask = m, .region = r});
                            break;
                        case 4:
                            cmd.hue_saturation(
                                image,
                                {.hue = epsilon, .lightness = epsilon, .mask = m, .region = r});
                            break;
                        case 5:
                            cmd.color_balance(image, {.shadows = {epsilon, 0, 0},
                                                      .midtones = {0, epsilon, 0},
                                                      .highlights = {0, 0, epsilon},
                                                      .mask = m,
                                                      .region = r});
                            break;
                        case 6:
                            cmd.channel_mixer(
                                image, {.red = {1 + epsilon, 0, 0, 0}, .mask = m, .region = r});
                            break;
                        case 7: {
                            ColorMatrixOptions o{
                                .domain = ColorEncoding::srgb, .mask = m, .region = r};
                            o.matrix[0] += epsilon;
                            cmd.color_matrix(image, o);
                            break;
                        }
                        default:
                            cmd.photo_filter(
                                image, {.density = std::abs(epsilon), .mask = m, .region = r});
                            break;
                        }
                    },
                    false, 6 * std::abs(epsilon) + 2e-6f);
            }
        }
    });
    test::run("signed levels and tangent curves have analytic HDR extensions", [&] {
        const std::array<Pixel, 1> input{encoded(1.25, .5, -.25)};
        // Gamma 1/2 squares the magnitude while retaining sign, including outside the cube.
        const std::array<Pixel, 1> squared{encoded(1.5625, .25, -.0625)};
        check(
            ctx, input, squared,
            [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                cmd.levels(image, {.composite = {.gamma = .5f}, .mask = m, .region = r});
            },
            false, 1e-5f);
        // For the review's three knots, the one-sided endpoint tangents are 1.004 and .996.
        const std::array points{Point{0, 0}, Point{.5f, .501f}, Point{1, 1}};
        const std::array<Pixel, 1> bent{encoded(1.249, .501, -.251)};
        check(
            ctx, input, bent,
            [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                cmd.curves(image, {.composite = points, .mask = m, .region = r});
            },
            false, 1e-5f);
        // SDR endpoints stay flat; HDR tails start at the table boundaries 0/1.
        const std::array line{Point{.25f, .375f}, Point{.75f, .625f}};
        const std::array<Pixel, 1> line_result{encoded(.75, .5, .25)};
        check(ctx, input, line_result,
              [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                  cmd.curves(image, {.composite = line, .mask = m, .region = r});
              });
    });
    test::run("1D tables reproduce affine functions beyond their sampled domain", [&] {
        const std::array<Pixel, 1> input{Pixel{1.4f, .5f, -.1f, 1}},
            expected{Pixel{1.85f, .5f, -.4f, 1}};
        const std::array<float, 3> table{-.25f, .5f, 1.25f};
        check(ctx, input, expected,
              [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                  cmd.lut(image, {.composite = table,
                                  .domain = ColorEncoding::linear,
                                  .mask = m,
                                  .region = r});
              });
        const std::array<Pixel, 1> encoded_input{encoded(1.25, .5, -.25)},
            encoded_output{encoded(1.5, 0, -1.5)};
        const std::array<float, 2> ramp{-1, 1};
        check(
            ctx, encoded_input, encoded_output,
            [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                cmd.lut(image, {.composite = ramp, .mask = m, .region = r});
            },
            false, 1e-5f);
    });
    test::run("HDR hue rotation permutes channels and desaturation halves chroma", [&] {
        const std::array<Pixel, 1> input{encoded(2, 1, -1)};
        const std::array<Pixel, 1> rotated{encoded(-1, 2, 1)},
            desaturated{encoded(1.25, .75, -.25)};
        check(
            ctx, input, rotated,
            [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                cmd.hue_saturation(image, {.hue = 120, .mask = m, .region = r});
            },
            false, 1e-5f);
        check(
            ctx, input, desaturated,
            [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                cmd.hue_saturation(image, {.saturation = -.5f, .mask = m, .region = r});
            },
            false, 1e-5f);
    });
    test::run("active mixers filters and black-white preserve signed HDR values", [&] {
        const std::array<Pixel, 1> input{encoded(1.25, .5, -.25)};
        const std::array<Pixel, 1> matrix{encoded(1.5, .5, -.25)}, mixer{encoded(2.5, .5, -.25)},
            filtered{encoded(1.25, .25, -.125)};
        for (int operation = 0; operation < 3; ++operation) {
            check(
                ctx, input,
                operation == 0   ? matrix
                : operation == 1 ? mixer
                                 : filtered,
                [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                    if (operation == 0) {
                        ColorMatrixOptions o{.domain = ColorEncoding::srgb, .mask = m, .region = r};
                        o.matrix[4] = .25f;
                        cmd.color_matrix(image, o);
                    } else if (operation == 1) {
                        cmd.channel_mixer(image, {.red = {2, 0, 0, 0}, .mask = m, .region = r});
                    } else {
                        cmd.photo_filter(image, {.color = {1, 0, 0, 1},
                                                 .density = .5f,
                                                 .preserve_luminosity = false,
                                                 .mask = m,
                                                 .region = r});
                    }
                },
                false, 2e-5f);
        }
        // At lightness .5 only the midtone band contributes: red += .7 * .5.
        // Original luma is .6053. Gamut compression pins R at 1.25 and keeps the
        // chroma direction; intersect that line with the R=1.25 plane.
        const std::array<Pixel, 1> balanced{encoded(1.6, .5, -.25)},
            balanced_luma{encoded(1.25, .6053 - .17971 * (.6447 / .92029),
                                  .6053 - .92971 * (.6447 / .92029))};
        for (bool keep : {false, true}) {
            check(
                ctx, input, keep ? balanced_luma : balanced,
                [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                    cmd.color_balance(image, {.midtones = {.5f, 0, 0},
                                              .preserve_luminosity = keep,
                                              .mask = m,
                                              .region = r});
                },
                false, 2e-5f);
        }
        // The same R=1.25 plane intersection for the filtered chroma direction.
        const std::array<Pixel, 1> filtered_luma{encoded(1.25, .6053 - .185525 * (.6447 / .814475),
                                                         .6053 - .560525 * (.6447 / .814475))};
        check(
            ctx, input, filtered_luma,
            [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                cmd.photo_filter(image,
                                 {.color = {1, 0, 0, 1}, .density = .5f, .mask = m, .region = r});
            },
            false, 2e-5f);
        const std::array grays{gray(2), gray(-.25)};
        check(
            ctx, grays, grays,
            [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                cmd.black_white(image, {.mask = m, .region = r});
            },
            false, 1e-5f);
    });
    test::run("master saturation uses its documented percentage-increase model", [&] {
        // HSL lightness .5: S=.2 -> .4, S=.8 -> 1 at +100%; gray remains gray.
        const std::array input{encoded(.6, .4, .4), encoded(.9, .1, .1), gray(.5)};
        const std::array output{encoded(.7, .3, .3), encoded(1, 0, 0), gray(.5)};
        check(ctx, input, output,
              [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                  cmd.hue_saturation(image, {.saturation = 1, .mask = m, .region = r});
              });
    });
    test::run("gray and colored gradient queries agree on hard-edge sides", [&] {
        // Encoded luma is .4998556 and .5001444 for the two non-gray probes.
        const std::array input{encoded(.5, .5, .498), encoded(.5, .5, .502), gray(.499),
                               gray(.501)};
        const std::array output{encoded(1, 0, 0), encoded(0, 0, 1), encoded(1, 0, 0),
                                encoded(0, 0, 1)};
        const std::array stops{GradientStop{0, {1, 0, 0, 1}}, GradientStop{.5f, {1, 0, 0, 1}},
                               GradientStop{.5f, {0, 0, 1, 1}}, GradientStop{1, {0, 0, 1, 1}}};
        check(ctx, input, output,
              [&](Commands& cmd, const Image& image, const Mask* m, std::optional<Rect> r) {
                  cmd.gradient_map(image, {.stops = stops, .mask = m, .region = r});
              });
    });
    test::run("range errors identify fields and bounds, including neutral options", [&] {
        auto image = ctx.create_image({1, 1});
        auto cmd = ctx.create_commands(1);
        try {
            cmd.levels(image, {.red = {.gamma = 0}});
            test::check(false, "expected levels error");
        } catch (const Error& e) {
            test::check(e.parameter() == "red.gamma", "missing exact field");
            const std::string message = e.what();
            test::check(message.find("red.gamma") != std::string::npos &&
                            message.find("0.01") != std::string::npos &&
                            message.find("9.99") != std::string::npos,
                        "missing field or bounds in message");
        }
        test::error(ErrorCode::invalid_argument, "levels", "green.input_white",
                    [&] { cmd.levels(image, {.green = {.input_white = 2}}); });
        test::error(ErrorCode::invalid_argument, "hue_saturation", "hue",
                    [&] { cmd.hue_saturation(image, {.hue = 270}); });
        test::error(ErrorCode::invalid_argument, "hue_saturation", "hue",
                    [&] { cmd.hue_saturation(image, {.hue = 361, .colorize = true}); });
        test::error(ErrorCode::invalid_argument, "lut", "domain",
                    [&] { cmd.lut(image, {.domain = ColorEncoding(9)}); });
        test::error(ErrorCode::invalid_argument, "levels", "region",
                    [&] { cmd.levels(image, {.region = Rect{0, 0, -1, 1}}); });
        const std::array<float, 24> cube{};
        test::error(ErrorCode::invalid_argument, "lut3d", "domain_max",
                    [&] { cmd.lut3d(image, {.values = cube, .domain_max = {0, 1, 1}}); });
        test::error(ErrorCode::invalid_argument, "lut3d", "domain_min", [&] {
            cmd.lut3d(image, {.values = cube,
                              .domain_min = {std::numeric_limits<float>::quiet_NaN(), 0, 0}});
        });
        cmd.fill(image, {.color = {0, 0, 0, 1}});
        ctx.submit_and_wait(cmd);
        ctx.destroy(image);
    });
    return test::finish();
}
