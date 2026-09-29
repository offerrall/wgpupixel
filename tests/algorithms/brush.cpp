#include "painting_reference.h"
#include <limits>

using namespace wgpupixel;
using reference::Canvas;
using reference::Pixel;

namespace {
void near_point(Point actual, double x, double y, double tolerance = 1e-4) {
    test::near(actual.x, float(x), float(tolerance));
    test::near(actual.y, float(y), float(tolerance));
}

Pixel straight(const Pixel& pixel) {
    return {pixel[0] / pixel[3], pixel[1] / pixel[3], pixel[2] / pixel[3], pixel[3]};
}

Pixel premultiply(double r, double g, double b, double a) {
    return {r * a, g * a, b * a, a};
}

double luma(double r, double g, double b) {
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

double tone(double v, double e, bool burn, ToneRange range) {
    const double third = e / 3;
    switch (range) {
    case ToneRange::shadows:
        if (burn) {
            return v < 0 ? v : std::max(v - third, 0.0) / (1 - third);
        }
        return third + v - third * v;
    case ToneRange::highlights:
        return v * (burn ? 1 - third : 1 + third);
    default:
        return v == 0 ? 0 : std::copysign(std::pow(std::abs(v), burn ? 1 + third : 1 / (1 + e)), v);
    }
}

Pixel dodge_burn(const Pixel& pixel, double e, bool burn, ToneRange range, bool protect) {
    if (pixel[3] <= 0) {
        return pixel;
    }
    const auto c = straight(pixel);
    std::array<double, 3> result;
    if (protect) {
        const double y = luma(c[0], c[1], c[2]);
        const double target =
            reference::srgb_decode(tone(reference::srgb_encode(y), e, burn, range));
        if (y <= 0) {
            for (int i = 0; i < 3; ++i) {
                result[i] = c[i] + target - y;
            }
        } else {
            for (int i = 0; i < 3; ++i) {
                result[i] = c[i] * target / y;
            }
            const double top = std::max({result[0], result[1], result[2]});
            const double limit = std::max({1.0, c[0], c[1], c[2]});
            if (top > limit && target < limit) {
                for (auto& v : result) {
                    v = target + (v - target) * (limit - target) / (top - target);
                }
            }
        }
    } else {
        for (int i = 0; i < 3; ++i) {
            result[i] = reference::srgb_decode(tone(reference::srgb_encode(c[i]), e, burn, range));
        }
    }
    return premultiply(result[0], result[1], result[2], pixel[3]);
}

Pixel sponge(const Pixel& pixel, double s, bool saturate, bool vibrance) {
    if (pixel[3] <= 0) {
        return pixel;
    }
    const auto c = straight(pixel);
    const double y = luma(c[0], c[1], c[2]);
    if (!saturate) {
        return premultiply(c[0] + (y - c[0]) * s, c[1] + (y - c[1]) * s, c[2] + (y - c[2]) * s,
                           pixel[3]);
    }
    const double high = std::max({c[0], c[1], c[2]}), low = std::min({c[0], c[1], c[2]});
    double factor = 1 + s;
    if (vibrance && high > 0) {
        factor = 1 + s * (1 - std::clamp((high - low) / high, 0.0, 1.0));
    }
    if (low < y && y >= 0) {
        factor = std::min(factor, y / (y - low));
    }
    if (vibrance && high > y) {
        factor = std::min(factor, (std::max(1.0, high) - y) / (high - y));
    }
    factor = std::max(factor, 1.0);
    return premultiply(y + (c[0] - y) * factor, y + (c[1] - y) * factor, y + (c[2] - y) * factor,
                       pixel[3]);
}

Pixel texel(const Canvas& image, int x, int y, bool wrap) {
    if (wrap) {
        x = ((x % image.width) + image.width) % image.width;
        y = ((y % image.height) + image.height) % image.height;
    } else if (x < 0 || y < 0 || x >= image.width || y >= image.height) {
        return {};
    }
    return image.at(x, y);
}

Pixel bilinear(const Canvas& image, double x, double y, bool wrap) {
    const double qx = x - 0.5, qy = y - 0.5;
    const double ox = std::floor(qx), oy = std::floor(qy);
    const int px = int(ox), py = int(oy);
    return reference::mix(
        reference::mix(texel(image, px, py, wrap), texel(image, px + 1, py, wrap), qx - ox),
        reference::mix(texel(image, px, py + 1, wrap), texel(image, px + 1, py + 1, wrap), qx - ox),
        qy - oy);
}

Pixel binomial(const Canvas& image, int x, int y) {
    constexpr std::array<double, 5> weights{1, 4, 6, 4, 1};
    Pixel sum{};
    for (int j = 0; j < 5; ++j) {
        for (int i = 0; i < 5; ++i) {
            const auto& p = image.at(std::clamp(x + i - 2, 0, image.width - 1),
                                     std::clamp(y + j - 2, 0, image.height - 1));
            for (int c = 0; c < 4; ++c) {
                sum[c] += weights[i] * weights[j] * p[c] / 256;
            }
        }
    }
    return sum;
}

const std::array<StrokeSample, 4> wavy{
    StrokeSample{{4.3f, 6.1f}, 0.3f, 0.2f, 10}, StrokeSample{{20.5f, 9.2f}, 0.9f, 0.6f, 40},
    StrokeSample{{31.7f, 22.8f}, 0.6f, 1.0f, -30}, StrokeSample{{12.2f, 27.4f}, 1.0f, 0.0f, 170}};
} // namespace

int main() {
    test::run("dabs follow spacing along the path, pressure and sampling", [] {
        const std::array line{StrokeSample{{10, 10}}, StrokeSample{{110, 10}}};
        auto placed = reference::dabs(line, {.diameter = 20, .spacing = 0.25f});
        test::check(placed.size() == 21, "spacing 25% of 20 px places a dab every 5 px");
        for (std::size_t i = 0; i < placed.size(); ++i) {
            near_point(placed[i].center, 10 + 5.0 * i, 10);
            test::near(placed[i].diameter, 20);
            test::near(placed[i].opacity, 1);
            test::near(placed[i].flow, 1);
        }
        // Spacing is measured along the polyline, across its corners.
        const std::array corner{StrokeSample{{0, 0}}, StrokeSample{{3, 0}}, StrokeSample{{3, 10}}};
        placed = reference::dabs(corner, {.diameter = 8, .spacing = 0.5f});
        test::check(placed.size() == 4, "corner dab count");
        near_point(placed[1].center, 3, 1);
        near_point(placed[2].center, 3, 5);
        near_point(placed[3].center, 3, 9);
        // Pressure drives size, opacity and flow; spacing follows the current size.
        const std::array soft{StrokeSample{{0, 0}, 0}, StrokeSample{{100, 0}, 0}};
        placed = reference::dabs(soft, {.diameter = 20,
                                        .spacing = 0.5f,
                                        .minimum_size = 0.5f,
                                        .minimum_opacity = 0.25f,
                                        .minimum_flow = 0.75f});
        test::check(placed.size() == 21, "spacing follows the pressure-driven size");
        test::near(placed[3].center.x, 15);
        test::near(placed[3].diameter, 10);
        test::near(placed[3].opacity, 0.25f);
        test::near(placed[3].flow, 0.75f);
        const std::array ramp{StrokeSample{{0, 0}, 0}, StrokeSample{{10, 0}, 1}};
        placed = reference::dabs(ramp, {.diameter = 4, .spacing = 1, .minimum_size = 0.5f});
        test::check(placed.size() == 4, "ramp dab count");
        near_point(placed[1].center, 2, 0);
        test::near(placed[1].diameter, 2.4f);
        near_point(placed[2].center, 4.4, 0);
        near_point(placed[3].center, 7.28, 0);
        // Spacing 0 places one dab per sample, with the stroke direction.
        placed = reference::dabs(corner, {.diameter = 8,
                                          .angle = 5,
                                          .spacing = 0,
                                          .angle_control = BrushAngleControl::direction});
        test::check(placed.size() == 3, "one dab per sample");
        test::near(placed[0].angle, 5);
        test::near(placed[2].angle, 95);
        // Rotation interpolates along the shortest turn; tilt narrows the tip.
        const std::array turning{StrokeSample{{0, 0}, 1, 0, 170},
                                 StrokeSample{{10, 0}, 1, 1, -170}};
        placed = reference::dabs(turning, {.diameter = 10,
                                           .spacing = 0.5f,
                                           .minimum_roundness = 0.2f,
                                           .tilt_roundness = true,
                                           .angle_control = BrushAngleControl::rotation});
        test::check(placed.size() == 3, "turning dab count");
        test::near(placed[1].angle, 180, 1e-3f);
        test::near(placed[1].roundness, 0.6f);
        test::near(placed[2].roundness, 0.2f);
        // Sub-pixel dabs keep one pixel and scale flow by their area.
        const std::array dot{StrokeSample{{5, 5}, 0.25f}};
        placed = reference::dabs(dot, {.diameter = 2, .minimum_size = 0});
        test::near(placed[0].diameter, 1);
        test::near(placed[0].flow, 0.25f);
        test::check(reference::dabs({}, {}).empty(), "no samples, no dabs");
    });

    test::run("jitter is deterministic, bounded and seeded", [] {
        const Brush brush{.diameter = 30,
                          .spacing = 0.1f,
                          .size_jitter = 0.8f,
                          .opacity_jitter = 0.5f,
                          .flow_jitter = 0.5f,
                          .angle_jitter = 1,
                          .roundness_jitter = 1,
                          .minimum_roundness = 0.3f,
                          .scatter = 1,
                          .scatter_both_axes = true,
                          .seed = 7};
        const std::array line{StrokeSample{{0, 50}}, StrokeSample{{200, 50}}};
        const auto a = reference::dabs(line, brush);
        const auto b = reference::dabs(line, brush);
        auto other = brush;
        other.seed = 8;
        const auto c = reference::dabs(line, other);
        bool different = false, varied = false;
        for (std::size_t i = 0; i < a.size() && i < c.size(); ++i) {
            test::check(a[i].center == b[i].center && a[i].diameter == b[i].diameter &&
                            a[i].angle == b[i].angle && a[i].opacity == b[i].opacity,
                        "same seed must reproduce the stroke");
            different = different || a[i].center != c[i].center;
            varied = varied || a[i].diameter != a[0].diameter;
            test::check(a[i].diameter >= 6 - 1e-4f && a[i].diameter <= 30, "size jitter bound");
            test::check(a[i].opacity >= 0.5f - 1e-6f && a[i].opacity <= 1, "opacity bound");
            test::check(a[i].roundness >= 0.3f - 1e-6f && a[i].roundness <= 1, "roundness");
            test::check(std::abs(a[i].center.y - 50) <= 30, "scatter bound");
            test::check(a[i].angle >= -180 && a[i].angle <= 180, "angle range");
        }
        test::check(different && varied, "jitter must vary with seed and dab");
        std::array<BrushDab, 2> head;
        test::check(brush_dabs(line, brush, head) == a.size(), "total count");
        test::check(head[1].center == a[1].center, "partial output");
    });

    test::run("stroke bounds and smudge scratch size", [] {
        const std::array line{StrokeSample{{10.5f, 10.5f}}, StrokeSample{{30.5f, 10.5f}}};
        const auto bounds = stroke_bounds(line, {.diameter = 10});
        test::check(bounds.has_value(), "bounds");
        test::check(bounds->x == 4 && bounds->y == 4 && bounds->width == 33 && bounds->height == 13,
                    "bounds cover dab reach");
        test::check(!stroke_bounds({}, {}).has_value(), "empty bounds");
        test::check(
            smudge_stroke_requirements({1, 1}, {.brush = {.diameter = 20}}).workspace.bytes() ==
                25 * 25 * 16,
            "smudge size");
        test::error(ErrorCode::invalid_argument, "brush_dabs", "brush.diameter",
                    [&] { (void)brush_dabs(line, {.diameter = 0.5f}); });
        test::error(ErrorCode::invalid_argument, "stroke_bounds", "brush.hardness",
                    [&] { (void)stroke_bounds(line, {.hardness = 2}); });
        test::error(ErrorCode::invalid_argument, "smudge_stroke_requirements", "brush.roundness",
                    [&] { (void)smudge_stroke_requirements({1, 1}, {.brush = {.roundness = 0}}); });
        const std::array bad{StrokeSample{{0, 0}, 1.5f}};
        test::error(ErrorCode::invalid_argument, "brush_dabs", "samples",
                    [&] { (void)brush_dabs(bad, {}); });
        const std::array far{StrokeSample{{0, 0}}, StrokeSample{{1e30f, 0}}};
        test::error(ErrorCode::capacity, "brush_dabs", "samples",
                    [&] { (void)brush_dabs(far, {}); });
    });

    test::run("opacity caps a stroke while flow builds up", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({9, 9});
        std::vector<StrokeSample> taps(12, StrokeSample{{4.5f, 4.5f}});
        const Brush hard{.diameter = 6, .spacing = 0};
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.fill(image, {.color = {0, 0, 0, 0}});
            cmd.brush_stroke(
                image, {.samples = taps, .brush = hard, .color = {1, 0, 0, 1}, .opacity = 0.5f});
        });
        auto result = reference::download(ctx, image);
        test::near(float(result.at(4, 4)[3]), 0.5f, 1e-6f);
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.fill(image, {.color = {0, 0, 0, 0}});
            cmd.brush_stroke(image, {.samples = std::span(taps).first(3),
                                     .brush = hard,
                                     .color = {1, 0, 0, 1},
                                     .flow = 0.25f});
        });
        result = reference::download(ctx, image);
        test::near(float(result.at(4, 4)[3]), float(1 - std::pow(0.75, 3)), 1e-6f);
        // Separate calls composite separately: coverage builds past the cap.
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.fill(image, {.color = {0, 0, 0, 0}});
            for (int i = 0; i < 2; ++i) {
                cmd.brush_stroke(image, {.samples = std::span(taps).first(1),
                                         .brush = hard,
                                         .color = {1, 0, 0, 1},
                                         .opacity = 0.5f});
            }
        });
        result = reference::download(ctx, image);
        test::near(float(result.at(4, 4)[3]), 0.75f, 1e-6f);
    });

    test::run("brush and eraser match the reference with masks, regions and modes", [] {
        auto ctx = Context::create();
        constexpr int width = 37, height = 33;
        auto image = ctx.create_image({width, height});
        auto mask = ctx.create_mask({width, height});
        const auto mask_bytes = reference::ramp_mask(width, height);
        reference::upload(ctx, mask, mask_bytes);
        const auto background = reference::pattern(width, height, 3);
        const std::array brushes{Brush{.diameter = 9, .hardness = 1, .spacing = 0.3f},
                                 Brush{.diameter = 12.5f,
                                       .hardness = 0.3f,
                                       .roundness = 0.4f,
                                       .angle = 30,
                                       .spacing = 0.2f,
                                       .minimum_size = 0.4f,
                                       .minimum_opacity = 0.5f,
                                       .minimum_flow = 0.2f},
                                 Brush{.diameter = 7,
                                       .hardness = 0,
                                       .spacing = 0.15f,
                                       .size_jitter = 0.5f,
                                       .angle_jitter = 1,
                                       .roundness_jitter = 0.7f,
                                       .angle_control = BrushAngleControl::direction,
                                       .scatter = 0.5f,
                                       .seed = 3},
                                 Brush{.diameter = 1, .spacing = 0.5f, .minimum_size = 0}};
        const Color color{0.3f, 0.12f, 0.05f, 0.6f};
        const Rect region{5, -3, 20, 25};
        for (const auto& brush : brushes) {
            for (bool erase : {false, true}) {
                for (int selection = 0; selection < 3; ++selection) {
                    for (auto mode : {BlendMode::normal, BlendMode::multiply}) {
                        if (erase && mode != BlendMode::normal) {
                            continue;
                        }
                        const reference::Selection chosen{
                            selection == 1 ? std::span<const std::uint8_t>(mask_bytes)
                                           : std::span<const std::uint8_t>{},
                            selection == 2 ? std::optional<Rect>{region} : std::nullopt};
                        reference::upload(ctx, image, background);
                        ctx.run_and_wait([&](Commands& cmd) {
                            const auto* selected = selection == 1 ? &mask : nullptr;
                            if (erase) {
                                cmd.eraser_stroke(image, {.samples = wavy,
                                                          .brush = brush,
                                                          .opacity = 0.8f,
                                                          .flow = 0.7f,
                                                          .mask = selected,
                                                          .region = chosen.region});
                            } else {
                                cmd.brush_stroke(image, {.samples = wavy,
                                                         .brush = brush,
                                                         .color = color,
                                                         .mode = mode,
                                                         .opacity = 0.8f,
                                                         .flow = 0.7f,
                                                         .mask = selected,
                                                         .region = chosen.region});
                            }
                        });
                        const auto coverage =
                            reference::stroke(width, height, wavy, brush, 0.8, 0.7);
                        const auto expected = reference::apply(
                            background, coverage, chosen, [&](const Pixel& p, int, int, double s) {
                                if (erase) {
                                    return reference::mix(p, Pixel{}, s);
                                }
                                return reference::blend({color.r, color.g, color.b, color.a}, p, s,
                                                        mode);
                            });
                        reference::expect(reference::download(ctx, image), expected, 2e-5);
                    }
                }
            }
        }
    });

    test::run("sampled tips scale, rotate and filter like the reference", [] {
        auto ctx = Context::create();
        constexpr int width = 40, height = 36;
        auto image = ctx.create_image({width, height});
        const reference::Tip tip{7, 4, {0,   40,  255, 255, 90, 0,   10,  //
                                        255, 255, 255, 0,   0,  255, 128, //
                                        30,  255, 60,  0,   0,  255, 255, //
                                        0,   0,   255, 255, 70, 200, 0}};
        auto tip_mask = ctx.create_mask({tip.width, tip.height});
        reference::upload(ctx, tip_mask, tip.coverage);
        const auto background = reference::pattern(width, height, 11);
        for (float diameter : {3.0f, 12.0f, 33.0f}) {
            for (float angle : {0.0f, 90.0f, 37.0f}) {
                const Brush brush{.diameter = diameter,
                                  .roundness = 0.7f,
                                  .angle = angle,
                                  .spacing = 0.6f,
                                  .tip = &tip_mask};
                reference::upload(ctx, image, background);
                ctx.run_and_wait([&](Commands& cmd) {
                    cmd.brush_stroke(image, {.samples = wavy,
                                             .brush = brush,
                                             .color = {0.1f, 0.4f, 0.2f, 0.9f},
                                             .flow = 0.8f});
                });
                const auto coverage = reference::stroke(width, height, wavy, brush, 1, 0.8, &tip);
                const auto expected = reference::apply(
                    background, coverage, {}, [](const Pixel& p, int, int, double s) {
                        return reference::blend({0.1, 0.4, 0.2, 0.9}, p, s, BlendMode::normal);
                    });
                reference::expect(reference::download(ctx, image), expected, 5e-5);
            }
        }
    });

    test::run("clone stamp samples source at position minus offset", [] {
        auto ctx = Context::create();
        constexpr int width = 30, height = 28;
        auto image = ctx.create_image({width, height});
        auto source = ctx.create_image({23, 19});
        auto tip_mask = ctx.create_mask({2, 2});
        const std::vector<std::uint8_t> square(4, 255);
        reference::upload(ctx, tip_mask, square);
        const auto background = reference::pattern(width, height, 5);
        const auto sampled = reference::pattern(23, 19, 6);
        reference::upload(ctx, source, sampled);
        // A hard square tip copies exact source pixels at an integer offset.
        const std::array tap{StrokeSample{{15, 14}}};
        reference::upload(ctx, image, background);
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.clone_stroke(
                source, image,
                {.samples = tap, .brush = {.diameter = 8, .tip = &tip_mask}, .offset = {6, 4}});
        });
        auto result = reference::download(ctx, image);
        for (int y = 12; y < 16; ++y) {
            for (int x = 13; x < 17; ++x) {
                const auto expected = reference::blend(sampled.at(x - 6, y - 4),
                                                       background.at(x, y), 1, BlendMode::normal);
                for (int c = 0; c < 4; ++c) {
                    test::near(float(result.at(x, y)[c]), float(expected[c]), 1e-6f);
                }
            }
        }
        const Brush brush{.diameter = 8, .hardness = 0.5f, .spacing = 0.3f};
        for (Point offset : {Point{3, -2}, Point{-4.25f, 7.5f}}) {
            for (auto mode : {BlendMode::normal, BlendMode::screen}) {
                reference::upload(ctx, image, background);
                ctx.run_and_wait([&](Commands& cmd) {
                    cmd.clone_stroke(source, image,
                                     {.samples = wavy,
                                      .brush = brush,
                                      .offset = offset,
                                      .mode = mode,
                                      .opacity = 0.9f});
                });
                const auto coverage = reference::stroke(width, height, wavy, brush, 0.9, 1);
                const auto expected = reference::apply(
                    background, coverage, {}, [&](const Pixel& p, int x, int y, double s) {
                        return reference::blend(
                            bilinear(sampled, x + 0.5 - offset.x, y + 0.5 - offset.y, false), p, s,
                            mode);
                    });
                reference::expect(reference::download(ctx, image), expected, 3e-5);
            }
        }
        // The same stroke with a sampled tip binds the source differently.
        reference::upload(ctx, image, background);
        const Brush tipped{.diameter = 6, .spacing = 0.5f, .tip = &tip_mask};
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.clone_stroke(source, image, {.samples = wavy, .brush = tipped, .offset = {-2, 3}});
        });
        const reference::Tip square_tip{2, 2, square};
        const auto coverage = reference::stroke(width, height, wavy, tipped, 1, 1, &square_tip);
        const auto expected =
            reference::apply(background, coverage, {}, [&](const Pixel& p, int x, int y, double s) {
                return reference::blend(texel(sampled, x + 2, y - 3, false), p, s,
                                        BlendMode::normal);
            });
        reference::expect(reference::download(ctx, image), expected, 3e-5);
    });

    test::run("pattern stamp tiles the pattern through its transform", [] {
        auto ctx = Context::create();
        constexpr int width = 34, height = 30;
        auto image = ctx.create_image({width, height});
        auto pattern = ctx.create_image({5, 3});
        const auto tile = reference::pattern(5, 3, 9, false);
        reference::upload(ctx, pattern, tile);
        const auto background = reference::pattern(width, height, 10);
        const Brush brush{.diameter = 10, .hardness = 0.7f, .spacing = 0.25f};
        for (const auto& transform :
             {Affine::translate(2, 1), Affine::rotate(30, {4, 4}) * Affine::scale(1.5f)}) {
            reference::upload(ctx, image, background);
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.pattern_stroke(
                    pattern, image,
                    {.samples = wavy, .brush = brush, .transform = transform, .flow = 0.6f});
            });
            const auto inverse_transform = *inverse(transform);
            const auto coverage = reference::stroke(width, height, wavy, brush, 1, 0.6);
            const auto expected = reference::apply(
                background, coverage, {}, [&](const Pixel& p, int x, int y, double s) {
                    const auto q = inverse_transform.map({x + 0.5f, y + 0.5f});
                    return reference::blend(bilinear(tile, q.x, q.y, true), p, s,
                                            BlendMode::normal);
                });
            reference::expect(reference::download(ctx, image), expected, 5e-5);
        }
    });

    test::run("dodge, burn and sponge follow their tone curves", [] {
        auto ctx = Context::create();
        constexpr int width = 36, height = 32;
        auto image = ctx.create_image({width, height});
        auto background = reference::pattern(width, height, 12);
        background.at(20, 12) = {0, 0, 0, 1};
        background.at(21, 12) = {0.9, 0.05, 0.02, 0.9};
        const Brush brush{.diameter = 14, .hardness = 0.5f, .spacing = 0.2f};
        const auto mask_bytes = reference::ramp_mask(width, height);
        auto mask = ctx.create_mask({width, height});
        reference::upload(ctx, mask, mask_bytes);
        for (auto range : {ToneRange::shadows, ToneRange::midtones, ToneRange::highlights}) {
            for (bool burn : {false, true}) {
                for (bool protect : {false, true}) {
                    reference::upload(ctx, image, background);
                    ctx.run_and_wait([&](Commands& cmd) {
                        cmd.dodge_burn_stroke(image, {.samples = wavy,
                                                      .brush = brush,
                                                      .burn = burn,
                                                      .range = range,
                                                      .exposure = 0.7f,
                                                      .protect_tones = protect,
                                                      .flow = 0.8f,
                                                      .mask = &mask});
                    });
                    const auto coverage = reference::stroke(width, height, wavy, brush, 0.7, 0.8);
                    const auto expected =
                        reference::apply(background, coverage, {mask_bytes, std::nullopt},
                                         [&](const Pixel& p, int, int, double s) {
                                             return dodge_burn(p, s, burn, range, protect);
                                         });
                    reference::expect(reference::download(ctx, image), expected, 2e-4);
                }
            }
        }
        for (bool saturate : {false, true}) {
            for (bool vibrance : {false, true}) {
                reference::upload(ctx, image, background);
                ctx.run_and_wait([&](Commands& cmd) {
                    cmd.sponge_stroke(image, {.samples = wavy,
                                              .brush = brush,
                                              .saturate = saturate,
                                              .flow = 0.6f,
                                              .vibrance = vibrance,
                                              .region = Rect{3, 3, 25, 20}});
                });
                const auto coverage = reference::stroke(width, height, wavy, brush, 1, 0.6);
                const auto expected =
                    reference::apply(background, coverage, {{}, Rect{3, 3, 25, 20}},
                                     [&](const Pixel& p, int, int, double s) {
                                         return sponge(p, s, saturate, vibrance);
                                     });
                reference::expect(reference::download(ctx, image), expected, 2e-5);
            }
        }
    });

    test::run("blur and sharpen read the pre-stroke pixels, also outside the region", [] {
        auto ctx = Context::create();
        constexpr int width = 31, height = 29;
        auto image = ctx.create_image({width, height});
        auto scratch = ctx.create_workspace(focus_stroke_requirements({width, height}).workspace);
        auto poison = ctx.create_image({width, height});
        const auto background = reference::pattern(width, height, 14);
        const Brush brush{.diameter = 11, .hardness = 0.6f, .spacing = 0.1f};
        const Rect region{8, 6, 12, 14};
        for (bool sharpen : {false, true}) {
            for (bool clipped : {false, true}) {
                reference::upload(ctx, image, background);
                ctx.run_and_wait([&](Commands& cmd) {
                    cmd.fill(poison, {.color = {9, 9, 9, 1}});
                    cmd.focus_stroke(poison,
                                     {.samples = wavy, .brush = brush, .workspace = scratch});
                    cmd.focus_stroke(
                        image, {.samples = wavy,
                                .brush = brush,
                                .sharpen = sharpen,
                                .strength = 0.8f,
                                .region = clipped ? std::optional<Rect>{region} : std::nullopt,
                                .workspace = scratch});
                });
                const auto coverage = reference::stroke(width, height, wavy, brush, 0.8, 1);
                const auto expected =
                    reference::apply(background, coverage,
                                     {{}, clipped ? std::optional<Rect>{region} : std::nullopt},
                                     [&](const Pixel& p, int x, int y, double s) {
                                         const auto blurred = binomial(background, x, y);
                                         if (!sharpen) {
                                             return reference::mix(p, blurred, s);
                                         }
                                         Pixel sharp;
                                         for (int c = 0; c < 4; ++c) {
                                             sharp[c] = p[c] + (p[c] - blurred[c]) * s;
                                         }
                                         if (!(sharp[3] > 0)) {
                                             return Pixel{};
                                         }
                                         const double alpha = std::min(sharp[3], 1.0);
                                         for (int c = 0; c < 3; ++c) {
                                             sharp[c] *= alpha / sharp[3];
                                         }
                                         sharp[3] = alpha;
                                         return sharp;
                                     });
                reference::expect(reference::download(ctx, image), expected, 2e-5);
            }
        }
    });

    test::run("brush tools validate atomically and skip empty strokes", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({8, 8});
        auto other = ctx.create_image({8, 8});
        auto small = ctx.create_workspace(focus_stroke_requirements({4, 8}).workspace);
        auto focus_workspace =
            ctx.create_workspace(focus_stroke_requirements(image.size()).workspace);
        auto wrong_mask = ctx.create_mask({4, 4});
        const std::array tap{StrokeSample{{4, 4}}};
        auto cmd = ctx.create_commands(2);
        cmd.fill(image, {.color = {0.2f, 0.3f, 0.4f, 1}});
        test::error(ErrorCode::invalid_argument, "brush_stroke", "brush.diameter",
                    [&] { cmd.brush_stroke(image, {.samples = tap, .brush = {.diameter = 0}}); });
        test::error(ErrorCode::invalid_argument, "brush_stroke", "brush.spacing",
                    [&] { cmd.brush_stroke(image, {.samples = tap, .brush = {.spacing = -1}}); });
        test::error(ErrorCode::invalid_argument, "brush_stroke", "brush.angle_control", [&] {
            cmd.brush_stroke(image,
                             {.samples = tap, .brush = {.angle_control = BrushAngleControl(9)}});
        });
        test::error(ErrorCode::invalid_argument, "brush_stroke", "opacity",
                    [&] { cmd.brush_stroke(image, {.samples = tap, .opacity = 1.5f}); });
        test::error(ErrorCode::invalid_argument, "brush_stroke", "mode",
                    [&] { cmd.brush_stroke(image, {.samples = tap, .mode = BlendMode(99)}); });
        test::error(ErrorCode::invalid_argument, "brush_stroke", "color",
                    [&] { cmd.brush_stroke(image, {.samples = tap, .color = {0, 0, 0, 2}}); });
        test::error(ErrorCode::invalid_argument, "eraser_stroke", "flow", [&] {
            cmd.eraser_stroke(image,
                              {.samples = tap, .flow = std::numeric_limits<float>::quiet_NaN()});
        });
        test::error(ErrorCode::invalid_argument, "eraser_stroke", "mask",
                    [&] { cmd.eraser_stroke(image, {.samples = tap, .mask = &wrong_mask}); });
        test::error(ErrorCode::invalid_argument, "clone_stroke", "destination",
                    [&] { cmd.clone_stroke(image, image, {.samples = tap}); });
        test::error(ErrorCode::invalid_argument, "clone_stroke", "offset", [&] {
            cmd.clone_stroke(
                other, image,
                {.samples = tap, .offset = {std::numeric_limits<float>::infinity(), 0}});
        });
        test::error(ErrorCode::invalid_argument, "pattern_stroke", "transform", [&] {
            cmd.pattern_stroke(other, image, {.samples = tap, .transform = Affine::scale(0)});
        });
        test::error(ErrorCode::invalid_argument, "dodge_burn_stroke", "range",
                    [&] { cmd.dodge_burn_stroke(image, {.samples = tap, .range = ToneRange(5)}); });
        test::error(ErrorCode::invalid_argument, "dodge_burn_stroke", "exposure",
                    [&] { cmd.dodge_burn_stroke(image, {.samples = tap, .exposure = -0.1f}); });
        test::error(ErrorCode::invalid_argument, "sponge_stroke", "samples", [&] {
            const std::array bad{StrokeSample{{std::numeric_limits<float>::quiet_NaN(), 0}}};
            cmd.sponge_stroke(image, {.samples = bad});
        });
        test::error(ErrorCode::capacity, "focus_stroke", "workspace",
                    [&] { cmd.focus_stroke(image, {.samples = tap, .workspace = small}); });
        test::error(ErrorCode::invalid_argument, "focus_stroke", "region", [&] {
            cmd.focus_stroke(
                image, {.samples = tap, .region = Rect{0, 0, -1, 1}, .workspace = focus_workspace});
        });
        test::error(ErrorCode::invalid_argument, "focus_stroke", "strength", [&] {
            cmd.focus_stroke(image, {.samples = tap, .strength = 2, .workspace = focus_workspace});
        });
        // Focus needs two records; a budget that prevents growth rejects the entire call.
        ctx.set_memory_limit(ctx.memory().total);
        test::error(ErrorCode::capacity, "focus_stroke", "memory_limit", [&] {
            cmd.focus_stroke(image, {.samples = tap, .workspace = focus_workspace});
        });
        ctx.set_memory_limit(0);
        // Empty strokes, zero opacity, and strokes outside the region record nothing.
        cmd.brush_stroke(image, {.samples = {}, .color = {1, 0, 0, 1}});
        cmd.brush_stroke(image, {.samples = tap, .color = {1, 0, 0, 1}, .opacity = 0});
        cmd.brush_stroke(image,
                         {.samples = tap, .color = {1, 0, 0, 1}, .region = Rect{0, 0, 0, 8}});
        const std::array outside{StrokeSample{{-100, 4}}};
        cmd.eraser_stroke(image, {.samples = outside});
        cmd.focus_stroke(image, {.samples = outside, .workspace = focus_workspace});
        const std::array center{StrokeSample{{4.5f, 4.5f}}};
        cmd.eraser_stroke(image, {.samples = center, .brush = {.diameter = 2}});
        ctx.submit_and_wait(cmd);
        const auto result = reference::download(ctx, image);
        test::near(float(result.at(0, 0)[3]), 1);
        test::near(float(result.at(4, 4)[3]), 0);
    });
    test::run("finite giant brushes cover the canvas without overflowing tile sizes", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({17, 9});
        const std::array tap{StrokeSample{{8.5f, 4.5f}}};
        for (float diameter : {1e10f, 1e19f, 1e20f, std::numeric_limits<float>::max()}) {
            auto cmd = ctx.create_commands();
            cmd.fill(image, {.color = {0, 0, 0, 0}});
            cmd.brush_stroke(image,
                             {.samples = tap, .brush = {.diameter = diameter},
                              .color = {1, 0, 0, 1}});
            ctx.submit_and_wait(cmd);
            const auto painted = test::read(ctx, image);
            for (std::size_t i = 0; i < painted.size(); ++i) {
                test::check(painted[i] == std::array<std::uint8_t, 4>{255, 0, 0, 255}[i % 4],
                            "a centered giant brush must fully cover this small canvas");
            }
            cmd.eraser_stroke(image, {.samples = tap, .brush = {.diameter = diameter}});
            ctx.submit_and_wait(cmd);
            for (const auto value : test::read(ctx, image)) {
                test::check(value == 0, "a centered giant eraser must clear the canvas");
            }
        }
    });
    return test::finish();
}
