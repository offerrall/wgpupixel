#include "painting_reference.h"
#include <limits>

using namespace wgpupixel;
using reference::Canvas;
using reference::Pixel;

namespace {
Pixel color_of(const Color& c) {
    return {c.r, c.g, c.b, c.a};
}

Pixel perceptual(const Pixel& a, const Pixel& b, double w) {
    const double alpha = a[3] + (b[3] - a[3]) * w;
    if (alpha <= 0) {
        return {};
    }
    Pixel result{0, 0, 0, alpha};
    for (int c = 0; c < 3; ++c) {
        const double first = a[3] > 0 ? reference::srgb_encode(a[c] / a[3]) * a[3] : 0;
        const double second = b[3] > 0 ? reference::srgb_encode(b[c] / b[3]) * b[3] : 0;
        result[c] = reference::srgb_decode((first + (second - first) * w) / alpha) * alpha;
    }
    return result;
}

// Returns nothing for pixels within `margin` of a discontinuity or outside the ramp.
std::optional<Pixel> ramp(const GradientFillOptions& o, double x, double y, bool& ambiguous) {
    const double vx = x - o.start.x, vy = y - o.start.y;
    const double ax = double(o.end.x) - o.start.x, ay = double(o.end.y) - o.start.y;
    const double squared = ax * ax + ay * ay;
    const double along = vx * ax + vy * ay, across = ax * vy - ay * vx;
    double t = along / squared;
    switch (o.shape) {
    case GradientShape::radial:
        t = std::hypot(vx, vy) / std::sqrt(squared);
        break;
    case GradientShape::angle: {
        const double turn = std::atan2(across, along) / (2 * std::numbers::pi);
        t = turn - std::floor(turn);
        ambiguous = ambiguous || t < 1e-3 || t > 1 - 1e-3;
        break;
    }
    case GradientShape::reflected:
        t = std::abs(t);
        break;
    case GradientShape::diamond:
        t = (std::abs(along) + std::abs(across)) / squared;
        break;
    default:
        break;
    }
    const auto near_integer = std::abs(t - std::round(t)) < 1e-3;
    switch (o.extend) {
    case EdgeMode::transparent:
        ambiguous = ambiguous || (near_integer && std::abs(t - 0.5) > 0.4);
        if (t < 0 || t > 1) {
            return std::nullopt;
        }
        break;
    case EdgeMode::repeat:
        ambiguous = ambiguous || near_integer;
        t -= std::floor(t);
        break;
    case EdgeMode::mirror: {
        const double m = t - 2 * std::floor(t / 2);
        t = m > 1 ? 2 - m : m;
        break;
    }
    default:
        t = std::clamp(t, 0.0, 1.0);
    }
    if (o.reverse) {
        t = 1 - t;
    }
    std::size_t high = 0;
    while (high < o.stops.size() && o.stops[high].position <= t) {
        ++high;
    }
    for (const auto& stop : o.stops) {
        ambiguous = ambiguous || std::abs(stop.position - t) < 1e-4;
    }
    if (high == 0) {
        return color_of(o.stops.front().color);
    }
    if (high == o.stops.size()) {
        return color_of(o.stops.back().color);
    }
    const auto& a = o.stops[high - 1];
    const auto& b = o.stops[high];
    const double w = (t - a.position) / (double(b.position) - a.position);
    if (o.interpolation == GradientInterpolation::perceptual) {
        return perceptual(color_of(a.color), color_of(b.color), w);
    }
    return reference::mix(color_of(a.color), color_of(b.color), w);
}

void compare(const Canvas& actual, const Canvas& before, const GradientFillOptions& o,
             const reference::Selection& selection) {
    for (int y = 0; y < before.height; ++y) {
        for (int x = 0; x < before.width; ++x) {
            bool ambiguous = false;
            const auto color = ramp(o, x + 0.5, y + 0.5, ambiguous);
            if (ambiguous) {
                continue;
            }
            auto expected = before.at(x, y);
            if (color) {
                expected =
                    reference::mix(expected, reference::blend(*color, expected, o.opacity, o.mode),
                                   selection.at(x, y, before.width));
            }
            for (int c = 0; c < 4; ++c) {
                if (!(std::abs(actual.at(x, y)[c] - expected[c]) <= 1e-4)) {
                    test::check(false, "gradient pixel " + std::to_string(x) + "," +
                                           std::to_string(y) + " shape " +
                                           std::to_string(int(o.shape)) + " extend " +
                                           std::to_string(int(o.extend)) + ": expected " +
                                           std::to_string(expected[c]) + ", got " +
                                           std::to_string(actual.at(x, y)[c]));
                }
            }
        }
    }
}
} // namespace

int main() {
    test::run("gradient shapes, extends and stops match the reference", [] {
        auto ctx = Context::create();
        constexpr int width = 41, height = 29;
        auto image = ctx.create_image({width, height});
        const auto background = reference::pattern(width, height, 31);
        const std::array stops{GradientStop{0.1f, {0.8f, 0.1f, 0.05f, 1}},
                               GradientStop{0.35f, {0.02f, 0.2f, 0.4f, 0.5f}},
                               GradientStop{0.35f, {0.3f, 0.3f, 0.0f, 0.6f}},
                               GradientStop{0.9f, {0, 0, 0, 0}}};
        for (int shape = 0; shape < 5; ++shape) {
            for (int extend = 0; extend < 4; ++extend) {
                for (bool reverse : {false, true}) {
                    for (auto interpolation :
                         {GradientInterpolation::linear, GradientInterpolation::perceptual}) {
                        const GradientFillOptions options{.start = {15.3f, 11.7f},
                                                          .end = {27.1f, 17.9f},
                                                          .stops = stops,
                                                          .shape = GradientShape(shape),
                                                          .extend = EdgeMode(extend),
                                                          .interpolation = interpolation,
                                                          .reverse = reverse,
                                                          .dither = false,
                                                          .mode = shape == 2 ? BlendMode::multiply
                                                                             : BlendMode::normal,
                                                          .opacity = shape == 3 ? 0.6f : 1.0f};
                        reference::upload(ctx, image, background);
                        ctx.run_and_wait([&](Commands& cmd) { cmd.gradient_fill(image, options); });
                        compare(reference::download(ctx, image), background, options, {});
                    }
                }
            }
        }
    });

    test::run("gradient fill honors masks, regions and a single stop", [] {
        auto ctx = Context::create();
        constexpr int width = 23, height = 19;
        auto image = ctx.create_image({width, height});
        auto mask = ctx.create_mask({width, height});
        const auto mask_bytes = reference::ramp_mask(width, height);
        reference::upload(ctx, mask, mask_bytes);
        const auto background = reference::pattern(width, height, 32);
        const std::array stops{GradientStop{0, {0, 0.5f, 0, 0.5f}},
                               GradientStop{1, {0.9f, 0.9f, 0.1f, 1}}};
        GradientFillOptions options{.start = {2.2f, 3.1f},
                                    .end = {20.6f, 15.4f},
                                    .stops = stops,
                                    .shape = GradientShape::diamond,
                                    .dither = false,
                                    .mask = &mask,
                                    .region = Rect{3, 2, 15, 30}};
        reference::upload(ctx, image, background);
        ctx.run_and_wait([&](Commands& cmd) { cmd.gradient_fill(image, options); });
        compare(reference::download(ctx, image), background, options, {mask_bytes, options.region});
        const std::array single{GradientStop{0.4f, {0.1f, 0.2f, 0.3f, 1}}};
        options = {.start = {0, 0}, .end = {5, 0}, .stops = single, .dither = false};
        reference::upload(ctx, image, background);
        ctx.run_and_wait([&](Commands& cmd) { cmd.gradient_fill(image, options); });
        compare(reference::download(ctx, image), background, options, {});
    });

    test::run("dither stays within one 8-bit step and averages out", [] {
        auto ctx = Context::create();
        constexpr int width = 256, height = 16;
        auto image = ctx.create_image({width, height});
        const std::array stops{GradientStop{0, {0.02f, 0.03f, 0.05f, 1}},
                               GradientStop{1, {0.05f, 0.04f, 0.1f, 1}}};
        const GradientFillOptions smooth{
            .start = {0, 0}, .end = {256, 0}, .stops = stops, .dither = false};
        auto dithered_options = smooth;
        dithered_options.dither = true;
        ctx.run_and_wait([&](Commands& cmd) { cmd.gradient_fill(image, smooth); });
        const auto plain = reference::download(ctx, image);
        ctx.run_and_wait([&](Commands& cmd) { cmd.gradient_fill(image, dithered_options); });
        const auto dithered = reference::download(ctx, image);
        double sum = 0;
        int changed = 0;
        for (std::size_t i = 0; i < plain.pixels.size(); ++i) {
            test::check(dithered.pixels[i][3] == 1, "opaque alpha stays exact");
            for (int c = 0; c < 3; ++c) {
                const double delta = reference::srgb_encode(dithered.pixels[i][c]) -
                                     reference::srgb_encode(plain.pixels[i][c]);
                test::check(std::abs(delta) <= 1.0 / 255 + 1e-5, "dither exceeds one step");
                sum += delta;
                changed += delta != 0;
            }
        }
        test::check(std::abs(sum / (3.0 * plain.pixels.size())) < 0.03 / 255, "dither bias");
        test::check(changed > int(plain.pixels.size()), "dither adds noise");
        // Identical inputs dither identically.
        ctx.run_and_wait([&](Commands& cmd) { cmd.gradient_fill(image, dithered_options); });
        test::check(reference::download(ctx, image).pixels == dithered.pixels, "deterministic");
    });

    test::run("gradient fill validates its options", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({4, 4});
        auto cmd = ctx.create_commands(1);
        const std::array stops{GradientStop{0, {0, 0, 0, 1}}, GradientStop{1, {1, 1, 1, 1}}};
        const std::array unordered{GradientStop{0.5f, {0, 0, 0, 1}},
                                   GradientStop{0.2f, {1, 1, 1, 1}}};
        const std::array outside{GradientStop{1.5f, {0, 0, 0, 1}}};
        const std::array invalid{GradientStop{0, {0, 0, 0, 2}}};
        const auto expect = [&](std::string_view parameter, GradientFillOptions options) {
            test::error(ErrorCode::invalid_argument, "gradient_fill", parameter,
                        [&] { cmd.gradient_fill(image, options); });
        };
        expect("stops", {.end = {1, 0}});
        expect("stops", {.end = {1, 0}, .stops = unordered});
        expect("stops", {.end = {1, 0}, .stops = outside});
        expect("stops", {.end = {1, 0}, .stops = invalid});
        expect("end", {.start = {1, 1}, .end = {1, 1}, .stops = stops});
        expect("start", {.start = {std::numeric_limits<float>::infinity(), 0}, .stops = stops});
        expect("shape", {.end = {1, 0}, .stops = stops, .shape = GradientShape(9)});
        expect("extend", {.end = {1, 0}, .stops = stops, .extend = EdgeMode(9)});
        expect("interpolation",
               {.end = {1, 0}, .stops = stops, .interpolation = GradientInterpolation(4)});
        expect("mode", {.end = {1, 0}, .stops = stops, .mode = BlendMode(40)});
        expect("opacity", {.end = {1, 0}, .stops = stops, .opacity = -1});
        cmd.gradient_fill(image, {.end = {1, 0}, .stops = stops});
        ctx.submit_and_wait(cmd);
    });
    return test::finish();
}
