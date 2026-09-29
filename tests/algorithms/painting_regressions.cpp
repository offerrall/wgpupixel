// Regression tests whose expectations come from geometry, invariants or exact transport,
// not from the shader formulas.
#include "painting_reference.h"
#include <limits>

using namespace wgpupixel;
using reference::Canvas;
using reference::Pixel;

namespace {
double total_alpha(const Canvas& canvas) {
    double total = 0;
    for (const auto& pixel : canvas.pixels) {
        total += pixel[3];
    }
    return total;
}
} // namespace

int main() {
    test::run("hard elliptical dabs paint their geometric area at any subpixel offset", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({64, 64});
        for (float roundness : {1.0f, 0.3f, 0.05f, 0.01f}) {
            for (float angle : {0.0f, 37.0f, 90.0f}) {
                double lowest = 1e30, highest = 0;
                for (Point offset : {Point{0, 0}, Point{0.25f, 0.1f}, Point{0.49f, 0.49f}}) {
                    const std::array tap{StrokeSample{{32.5f + offset.x, 31.5f + offset.y}}};
                    ctx.run_and_wait([&](Commands& cmd) {
                        cmd.fill(image, {.color = {0, 0, 0, 0}});
                        cmd.brush_stroke(
                            image,
                            {.samples = tap,
                             .brush = {.diameter = 20, .roundness = roundness, .angle = angle},
                             .color = {1, 1, 1, 1}});
                    });
                    const double area = std::numbers::pi * 10 * 10 * roundness;
                    const double total = total_alpha(reference::download(ctx, image));
                    test::check(std::abs(total - area) <= 0.03 * area + 0.05,
                                "roundness " + std::to_string(roundness) + " angle " +
                                    std::to_string(angle) + ": area " + std::to_string(area) +
                                    ", painted " + std::to_string(total));
                    lowest = std::min(lowest, total);
                    highest = std::max(highest, total);
                }
                // Subpixel motion must not change the stroke weight.
                test::check(highest <= 1.04 * lowest, "painted area varies with subpixel offset");
            }
        }
    });

    test::run("sharpening keeps valid premultiplied pixels", [] {
        auto ctx = Context::create();
        // The reviewer's case: a faint pixel inside opaque blue sharpens to transparent.
        auto small = ctx.create_image({5, 5});
        auto small_scratch = ctx.create_workspace(focus_stroke_requirements({5, 5}).workspace);
        Canvas blue{5, 5, std::vector<Pixel>(25, Pixel{0, 0, 1, 1})};
        blue.at(2, 2) = {0.1, 0, 0, 0.1};
        reference::upload(ctx, small, blue);
        const std::array tap{StrokeSample{{2.5f, 2.5f}}};
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.focus_stroke(small, {.samples = tap,
                                     .brush = {.diameter = 5},
                                     .sharpen = true,
                                     .strength = 1,
                                     .workspace = small_scratch});
        });
        const auto center = reference::download(ctx, small).at(2, 2);
        test::check(center == Pixel{0, 0, 0, 0}, "transparent result must carry no color");
        // Random SDR content larger than a workgroup: alpha in [0, 1], color in [0, alpha].
        constexpr int width = 45, height = 38;
        auto image = ctx.create_image({width, height});
        auto scratch = ctx.create_workspace(focus_stroke_requirements({width, height}).workspace);
        reference::upload(ctx, image, reference::pattern(width, height, 77));
        const std::array cover{StrokeSample{{0, 19}}, StrokeSample{{45, 19}}};
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.focus_stroke(image, {.samples = cover,
                                     .brush = {.diameter = 80, .spacing = 0.05f},
                                     .sharpen = true,
                                     .strength = 1,
                                     .workspace = scratch});
        });
        for (const auto& p : reference::download(ctx, image).pixels) {
            test::check(p[3] >= 0 && p[3] <= 1, "alpha out of range");
            for (int c = 0; c < 3; ++c) {
                test::check(std::isfinite(p[c]) && (p[3] > 0 || p[c] == 0),
                            "transparent pixels must carry no color");
            }
        }
    });

    test::run("sharpening keeps HDR overshoot, undershoot and straight color", [] {
        auto ctx = Context::create();
        // Binomial weights 1 4 6 4 1 over 16 per axis, derived here independently.
        const auto weight = [](int d) { return std::array{1.0, 4.0, 6.0, 4.0, 1.0}[d + 2] / 16; };
        const auto sharpen = [&](Commands& cmd, const Image& image, const Workspace& scratch,
                                 int width, int height) {
            const std::array cover{StrokeSample{{0, height / 2.0f}},
                                   StrokeSample{{float(width), height / 2.0f}}};
            cmd.focus_stroke(image,
                             {.samples = cover,
                              .brush = {.diameter = 4.0f * float(width + height), .spacing = 0.05f},
                              .sharpen = true,
                              .strength = 1,
                              .workspace = scratch});
        };
        // The reviewer's case: opaque (2, 2, 2) in opaque black sharpens to 2 + 2 * (1 - 36/256).
        {
            auto image = ctx.create_image({5, 5});
            auto scratch = ctx.create_workspace(focus_stroke_requirements({5, 5}).workspace);
            Canvas black{5, 5, std::vector<Pixel>(25, Pixel{0, 0, 0, 1})};
            black.at(2, 2) = {2, 2, 2, 1};
            reference::upload(ctx, image, black);
            ctx.run_and_wait([&](Commands& cmd) { sharpen(cmd, image, scratch, 5, 5); });
            const auto center = reference::download(ctx, image).at(2, 2);
            for (int c = 0; c < 3; ++c) {
                test::near(float(center[c]), 3.71875f, 1e-5f);
            }
            test::near(float(center[3]), 1, 0);
        }
        // Isolated HDR impulses on an opaque black canvas larger than a workgroup: every
        // pixel is v * (2 * delta - w(dx) * w(dy)), including negative undershoot.
        constexpr int width = 40, height = 36;
        auto image = ctx.create_image({width, height});
        auto scratch = ctx.create_workspace(focus_stroke_requirements({width, height}).workspace);
        Canvas canvas{width, height, std::vector<Pixel>(width * height, Pixel{0, 0, 0, 1})};
        const std::array<std::array<int, 2>, 3> spots{{{7, 6}, {25, 17}, {33, 29}}};
        const std::array<double, 3> values{5, 40, 0.75};
        for (std::size_t i = 0; i < spots.size(); ++i) {
            canvas.at(spots[i][0], spots[i][1]) = {values[i], values[i] / 2, values[i] / 4, 1};
        }
        reference::upload(ctx, image, canvas);
        ctx.run_and_wait([&](Commands& cmd) { sharpen(cmd, image, scratch, width, height); });
        const auto result = reference::download(ctx, image);
        for (std::size_t i = 0; i < spots.size(); ++i) {
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    const double expected =
                        values[i] * ((dx == 0 && dy == 0 ? 2.0 : 0.0) - weight(dx) * weight(dy));
                    const auto& p = result.at(spots[i][0] + dx, spots[i][1] + dy);
                    test::near(float(p[0]), float(expected), 1e-5f * float(values[i]));
                    test::near(float(p[3]), 1, 0);
                }
            }
        }
        // A lone opaque pixel on transparency: alpha overshoots to 2 - 36/256, is clamped,
        // and the straight color is kept exactly; its neighbours turn transparent.
        Canvas lone{width, height, std::vector<Pixel>(width * height, Pixel{})};
        lone.at(20, 18) = {3, 0.25, -0.5, 1};
        reference::upload(ctx, image, lone);
        ctx.run_and_wait([&](Commands& cmd) { sharpen(cmd, image, scratch, width, height); });
        const auto isolated = reference::download(ctx, image);
        const std::array expected{3.0, 0.25, -0.5, 1.0};
        for (int c = 0; c < 4; ++c) {
            test::near(float(isolated.at(20, 18)[c]), float(expected[c]), 1e-5f);
            test::check(isolated.at(21, 18)[c] == 0 && isolated.at(18, 16)[c] == 0,
                        "neighbours become transparent");
        }
    });

    test::run("sharpening near the float limit keeps the straight color", [] {
        auto ctx = Context::create();
        const auto sharpen_all = [](Commands& cmd, const Image& image, const Workspace& scratch,
                                    float width, float height) {
            const std::array cover{StrokeSample{{0, height / 2}},
                                   StrokeSample{{width, height / 2}}};
            cmd.focus_stroke(image, {.samples = cover,
                                     .brush = {.diameter = 4 * (width + height), .spacing = 0.05f},
                                     .sharpen = true,
                                     .strength = 1,
                                     .workspace = scratch});
        };
        // The reviewer's case: (3e38, 0, 0, 1) alone on transparency. Alpha overshoots to
        // 2 - 36/256 and is clamped to 1, so the pixel must come back unchanged.
        {
            auto image = ctx.create_image({5, 5});
            auto scratch = ctx.create_workspace(focus_stroke_requirements({5, 5}).workspace);
            Canvas lone{5, 5, std::vector<Pixel>(25, Pixel{})};
            lone.at(2, 2) = {3e38, 0, 0, 1};
            reference::upload(ctx, image, lone);
            ctx.run_and_wait([&](Commands& cmd) { sharpen_all(cmd, image, scratch, 5, 5); });
            const auto center = reference::download(ctx, image).at(2, 2);
            test::check(std::abs(center[0] - 3e38) <= 3e38 * 1e-6 && center[1] == 0 &&
                            center[2] == 0 && center[3] == 1,
                        "expected (3e38, 0, 0, 1), got " + std::to_string(center[0]));
        }
        // Extreme positive and negative impulses on a canvas above one workgroup: each
        // keeps its straight color and every pixel stays finite.
        constexpr int width = 40, height = 36;
        auto image = ctx.create_image({width, height});
        auto scratch = ctx.create_workspace(focus_stroke_requirements({width, height}).workspace);
        Canvas canvas{width, height, std::vector<Pixel>(width * height, Pixel{})};
        const std::array<std::array<int, 2>, 3> spots{{{6, 5}, {21, 17}, {33, 30}}};
        const std::array<Pixel, 3> colors{Pixel{3.3e38, -3.3e38, 1e38, 1},
                                          Pixel{-2e38, 5e37, 3e38, 1}, Pixel{1, 2e38, -1, 1}};
        for (std::size_t i = 0; i < spots.size(); ++i) {
            canvas.at(spots[i][0], spots[i][1]) = colors[i];
        }
        reference::upload(ctx, image, canvas);
        ctx.run_and_wait([&](Commands& cmd) { sharpen_all(cmd, image, scratch, width, height); });
        const auto result = reference::download(ctx, image);
        for (const auto& p : result.pixels) {
            for (double channel : p) {
                test::check(std::isfinite(channel), "sharpening overflowed");
            }
        }
        for (std::size_t i = 0; i < spots.size(); ++i) {
            const auto& p = result.at(spots[i][0], spots[i][1]);
            for (int c = 0; c < 4; ++c) {
                const double expected = double(float(colors[i][c]));
                test::check(std::abs(p[c] - expected) <= 1e-6 * std::abs(expected),
                            "straight color changed: " + std::to_string(p[c]));
            }
        }
    });

    test::run("dodge and burn continue their curves beyond [0, 1]", [] {
        auto ctx = Context::create();
        constexpr int width = 24, height = 20;
        auto image = ctx.create_image({width, height});
        // Straight sRGB transfer from IEC 61966-2-1, mirrored for negatives.
        const auto encode = [](double v) {
            const double a = std::abs(v);
            return std::copysign(a <= 0.0031308 ? 12.92 * a : 1.055 * std::pow(a, 1 / 2.4) - 0.055,
                                 v);
        };
        const auto decode = [](double v) {
            const double a = std::abs(v);
            return std::copysign(a <= 0.04045 ? a / 12.92 : std::pow((a + 0.055) / 1.055, 2.4), v);
        };
        const std::array<double, 4> inputs{4.0, -0.25, 1.5, -2.0};
        const std::array cover{StrokeSample{{0, 10}}, StrokeSample{{24, 10}}};
        struct Case {
            ToneRange range;
            bool burn;
            double (*curve)(double, double);
        };
        const std::array cases{
            Case{ToneRange::highlights, false, [](double v, double e) { return v * (1 + e / 3); }},
            Case{ToneRange::midtones, false,
                 [](double v, double e) {
                     return std::copysign(std::pow(std::abs(v), 1 / (1 + e)), v);
                 }},
            Case{ToneRange::shadows, false,
                 [](double v, double e) { return e / 3 + v * (1 - e / 3); }},
            Case{ToneRange::shadows, true, [](double v, double e) {
                     return v < 0 ? v : std::max(0.0, (v - e / 3) / (1 - e / 3));
                 }}};
        for (const auto& item : cases) {
            Canvas canvas{width, height,
                          std::vector<Pixel>(width * height, Pixel{0.1, 0.1, 0.1, 1})};
            for (int x = 0; x < width; ++x) {
                const double v = inputs[std::size_t(x) % inputs.size()];
                canvas.at(x, 10) = {v * 0.5, v * 0.5, v * 0.5, 0.5};
            }
            reference::upload(ctx, image, canvas);
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.dodge_burn_stroke(image, {.samples = cover,
                                              .brush = {.diameter = 200},
                                              .burn = item.burn,
                                              .range = item.range,
                                              .exposure = 0.6f,
                                              .protect_tones = false});
            });
            const auto result = reference::download(ctx, image);
            for (int x = 0; x < width; ++x) {
                const double v = inputs[std::size_t(x) % inputs.size()];
                const double straight = decode(item.curve(encode(v), 0.6));
                test::near(float(result.at(x, 10)[0]), float(straight * 0.5),
                           2e-5f * float(std::max(1.0, std::abs(straight))));
                test::near(float(result.at(x, 10)[3]), 0.5f, 0);
            }
        }
    });

    test::run("perceptual gradients interpolate negative colors symmetrically", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({101, 9});
        const std::array stops{GradientStop{0, {-0.5f, -2, 0.25f, 1}},
                               GradientStop{1, {0.5f, 2, 0.25f, 1}}};
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.gradient_fill(image,
                              {.start = {0, 0}, .end = {101, 0}, .stops = stops, .dither = false});
        });
        // Pixel 50 sits at t = 0.5: mirrored encodings cancel to exactly zero.
        const auto middle = reference::download(ctx, image).at(50, 4);
        test::near(float(middle[0]), 0, 1e-6f);
        test::near(float(middle[1]), 0, 1e-6f);
        test::near(float(middle[2]), 0.25f, 1e-6f);
        test::near(float(middle[3]), 1, 0);
    });

    test::run("blur and sharpen preserve constant images, including large HDR", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({20, 17});
        auto scratch = ctx.create_workspace(focus_stroke_requirements({20, 17}).workspace);
        const std::array cover{StrokeSample{{0, 8}}, StrokeSample{{20, 8}}};
        for (const Color color : {Color{0.3f, 0.2f, 0.1f, 0.5f}, Color{1e37f, 1e37f, 1e37f, 1},
                                  Color{3e38f, 2e38f, 1e38f, 1}}) {
            for (bool sharpen : {false, true}) {
                ctx.run_and_wait([&](Commands& cmd) {
                    cmd.fill(image, {.color = color});
                    cmd.focus_stroke(image, {.samples = cover,
                                             .brush = {.diameter = 60, .spacing = 0.05f},
                                             .sharpen = sharpen,
                                             .strength = 1,
                                             .workspace = scratch});
                });
                const std::array expected{color.r, color.g, color.b, color.a};
                for (const auto& p : reference::download(ctx, image).pixels) {
                    for (int c = 0; c < 4; ++c) {
                        test::check(std::abs(p[c] - expected[c]) <= 1e-6 * expected[c],
                                    "constant channel " + std::to_string(c) + " expected " +
                                        std::to_string(expected[c]) + " got " +
                                        std::to_string(p[c]) + (sharpen ? " sharpen" : " blur"));
                    }
                }
            }
        }
    });

    test::run("smudge at strength 1 transports the first patch exactly", [] {
        auto ctx = Context::create();
        constexpr int width = 160, height = 100;
        auto image = ctx.create_image({width, height});
        auto tip = ctx.create_mask({2, 2});
        const std::vector<std::uint8_t> full(4, 255);
        reference::upload(ctx, tip, full);
        const auto original = reference::pattern(width, height, 91, false);
        // Dabs every 10 px from x = 40.5 to 100.5: a 60 px shift. Patches exceed 256 lanes.
        const std::array path{StrokeSample{{40.5f, 50.5f}}, StrokeSample{{100.5f, 50.5f}}};
        for (bool sampled : {false, true}) {
            const Brush brush{.diameter = 40, .spacing = 0.25f, .tip = sampled ? &tip : nullptr};
            const auto plan = smudge_stroke_requirements({1, 1}, {.brush = brush}).workspace;
            test::check((plan.bytes() / 16) > 256, "patch must exceed one workgroup");
            auto scratch = ctx.create_workspace(plan);
            reference::upload(ctx, image, original);
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.smudge_stroke(
                    image, {.samples = path, .brush = brush, .strength = 1, .workspace = scratch});
            });
            const auto result = reference::download(ctx, image);
            int checked = 0;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const double dx = x + 0.5 - 100.5, dy = y + 0.5 - 50.5;
                    // Fully covered by the last dab: the square tip's flat interior, or
                    // the round tip at least half a pixel inside its rim.
                    const bool inside = sampled ? std::max(std::abs(dx), std::abs(dy)) <= 9.5
                                                : std::hypot(dx, dy) <= 19.4;
                    if (!inside) {
                        continue;
                    }
                    ++checked;
                    for (int c = 0; c < 4; ++c) {
                        test::near(float(result.at(x, y)[c]), float(original.at(x - 60, y)[c]),
                                   1e-6f);
                    }
                }
            }
            test::check(checked > 300, "interior checked");
            ctx.destroy(scratch);
        }
    });

    test::run("large and long smudge strokes keep order across commands", [] {
        auto ctx = Context::create();
        // One 200 px dab per command (patch above 128x128), four dabs 50 px apart.
        {
            constexpr int width = 400, height = 260;
            auto image = ctx.create_image({width, height});
            const auto original = reference::pattern(width, height, 93, false);
            const Brush brush{.diameter = 200, .spacing = 0.25f};
            auto scratch = ctx.create_workspace(
                smudge_stroke_requirements({1, 1}, {.brush = brush}).workspace);
            const std::array path{StrokeSample{{100.5f, 130.5f}}, StrokeSample{{260.5f, 130.5f}}};
            const auto placed = reference::dabs(path, brush);
            test::check(placed.size() == 4 && placed.back().center.x == 250.5f, "wide dabs");
            reference::upload(ctx, image, original);
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.smudge_stroke(
                    image, {.samples = path, .brush = brush, .strength = 1, .workspace = scratch});
            });
            const auto result = reference::download(ctx, image);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    if (std::hypot(x + 0.5 - 250.5, y + 0.5 - 130.5) > 99.4) {
                        continue;
                    }
                    for (int c = 0; c < 4; ++c) {
                        test::near(float(result.at(x, y)[c]), float(original.at(x - 150, y)[c]),
                                   1e-6f);
                    }
                }
            }
        }
        // 2200 one-pixel steps of a 40 px brush span several single-workgroup commands.
        {
            constexpr int width = 2300, height = 60;
            auto image = ctx.create_image({width, height});
            const auto original = reference::pattern(width, height, 94, false);
            const Brush brush{.diameter = 40, .spacing = 0.025f};
            auto scratch = ctx.create_workspace(
                smudge_stroke_requirements({1, 1}, {.brush = brush}).workspace);
            const std::array path{StrokeSample{{40.5f, 30.5f}}, StrokeSample{{2240.5f, 30.5f}}};
            const auto placed = reference::dabs(path, brush);
            const int shift = int(placed.back().center.x - placed.front().center.x);
            test::check(placed.size() > 2100 && placed.back().center.x == 40.5f + float(shift),
                        "integer steps");
            reference::upload(ctx, image, original);
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.smudge_stroke(
                    image, {.samples = path, .brush = brush, .strength = 1, .workspace = scratch});
            });
            const auto result = reference::download(ctx, image);
            const double cx = placed.back().center.x;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    if (std::hypot(x + 0.5 - cx, y + 0.5 - 30.5) > 19.4) {
                        continue;
                    }
                    for (int c = 0; c < 4; ++c) {
                        test::near(float(result.at(x, y)[c]), float(original.at(x - shift, y)[c]),
                                   1e-6f);
                    }
                }
            }
        }
    });

    test::run("oversized diameters fail instead of overflowing sizes", [] {
        test::check(
            smudge_stroke_requirements({1, 1}, {.brush = {.diameter = 60000}}).workspace.bytes() ==
                60005ull * 60005 * 16,
            "large finite size");
        for (float diameter : {1e6f, 1e19f, 1e20f, std::numeric_limits<float>::max()}) {
            test::error(ErrorCode::capacity, "smudge_stroke_requirements", "brush.diameter", [&] {
                (void)smudge_stroke_requirements({1, 1}, {.brush = {.diameter = diameter}});
            });
        }
        auto ctx = Context::create();
        auto image = ctx.create_image({8, 8});
        auto scratch = ctx.create_workspace(focus_stroke_requirements({8, 8}).workspace);
        const std::array tap{StrokeSample{{4, 4}}, StrokeSample{{5, 4}}};
        auto cmd = ctx.create_commands(1);
        test::error(ErrorCode::capacity, "smudge_stroke", "brush.diameter", [&] {
            cmd.smudge_stroke(image,
                              {.samples = tap, .brush = {.diameter = 1e20f}, .workspace = scratch});
        });
        const std::array stops{GradientStop{0, {1, 0, 0, 1}}, GradientStop{1, {0, 0, 1, 1}}};
        test::error(ErrorCode::invalid_argument, "gradient_fill", "end",
                    [&] { cmd.gradient_fill(image, {.end = {1e-20f, -1e-20f}, .stops = stops}); });
    });
    test::run("work budgets reject strokes that would stall the GPU", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1024, 1024});
        auto cmd = ctx.create_commands(1024);
        // 420 passes of a 2500 px brush at 1% spacing: 17k dabs over all 16 tiles.
        std::vector<StrokeSample> zigzag;
        for (int i = 0; i < 420; ++i) {
            zigzag.push_back({{i % 2 ? 1024.0f : 0.0f, 512}});
        }
        test::error(ErrorCode::capacity, "brush_stroke", "samples", [&] {
            cmd.brush_stroke(image, {.samples = zigzag,
                                     .brush = {.diameter = 2500, .spacing = 0.01f},
                                     .color = {1, 0, 0, 1}});
        });
        // 18000 smudge dabs of a 1005 px patch: 1.8e10 patch updates.
        const Brush big{.diameter = 1000, .spacing = 0.01f};
        auto scratch =
            ctx.create_workspace(smudge_stroke_requirements({1, 1}, {.brush = big}).workspace);
        std::vector<StrokeSample> passes;
        for (int i = 0; i < 61; ++i) {
            passes.push_back({{i % 2 ? 3000.0f : 0.0f, 512}});
        }
        test::error(ErrorCode::capacity, "smudge_stroke", "samples", [&] {
            cmd.smudge_stroke(image, {.samples = passes, .brush = big, .workspace = scratch});
        });
        // Within budget, the same tools record; large smudge dabs take a command each.
        cmd.brush_stroke(image, {.samples = std::span(zigzag).first(40),
                                 .brush = {.diameter = 2500, .spacing = 0.01f},
                                 .color = {1, 0, 0, 1}});
        auto many = ctx.create_commands(128);
        const std::array shorter{StrokeSample{{0, 512}}, StrokeSample{{990, 512}}};
        many.smudge_stroke(image, {.samples = shorter, .brush = big, .workspace = scratch});
        ctx.submit_and_wait(many);
        ctx.submit_and_wait(cmd);
    });
    return test::finish();
}
