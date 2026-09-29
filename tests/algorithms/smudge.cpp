#include "painting_reference.h"

using namespace wgpupixel;
using reference::Canvas;
using reference::Pixel;

namespace {
// Sequential smudge: every dab mixes the carried patch with the current pixels, then
// deposits it, before the next dab reads the result.
Canvas smudge(const Canvas& before, std::span<const StrokeSample> samples, const Brush& brush,
              double strength, std::optional<Pixel> finger, const reference::Selection& selection,
              const reference::Tip* tip = nullptr) {
    const auto placed = reference::dabs(samples, brush);
    const double extent =
        tip ? std::hypot(tip->width + 1.0, tip->height + 1.0) / std::max(tip->width, tip->height)
            : 1;
    double largest = 1;
    for (const auto& dab : placed) {
        largest = std::max<double>(largest, dab.diameter);
    }
    const int half = int(std::ceil(0.5 * largest * extent + 1)) + 1;
    const int side = 2 * half + 1;
    std::vector<Pixel> carried(std::size_t(side) * side);
    auto canvas = before;
    for (std::size_t i = 0; i < placed.size(); ++i) {
        const auto& dab = placed[i];
        const int ox = int(std::floor(dab.center.x)) - half;
        const int oy = int(std::floor(dab.center.y)) - half;
        for (int ly = 0; ly < side; ++ly) {
            for (int lx = 0; lx < side; ++lx) {
                const int x = ox + lx, y = oy + ly;
                const bool inside = x >= 0 && y >= 0 && x < canvas.width && y < canvas.height;
                const Pixel current = inside ? canvas.at(x, y) : Pixel{};
                auto& slot = carried[std::size_t(ly) * side + lx];
                if (i == 0) {
                    slot = finger ? *finger : current;
                    continue;
                }
                slot = reference::mix(current, slot, strength);
                if (!inside) {
                    continue;
                }
                const double amount = reference::dab_coverage(dab, x, y, brush.hardness, tip) *
                                      dab.opacity * dab.flow * selection.at(x, y, canvas.width);
                if (amount > 0) {
                    canvas.at(x, y) = reference::mix(current, slot, amount);
                }
            }
        }
    }
    return canvas;
}

const std::array path{StrokeSample{{6.5f, 16.2f}, 1.0f}, StrokeSample{{18.2f, 12.9f}, 0.7f},
                      StrokeSample{{33.4f, 20.1f}, 0.4f}, StrokeSample{{22.0f, 30.5f}, 0.9f}};
} // namespace

int main() {
    test::run("smudge matches the sequential reference", [] {
        auto ctx = Context::create();
        constexpr int width = 40, height = 36;
        auto image = ctx.create_image({width, height});
        const auto background = reference::pattern(width, height, 21);
        const auto mask_bytes = reference::ramp_mask(width, height);
        auto mask = ctx.create_mask({width, height});
        reference::upload(ctx, mask, mask_bytes);
        const reference::Tip tip{
            3, 5, {255, 90, 0, 255, 255, 30, 128, 255, 200, 40, 255, 255, 0, 180, 255}};
        auto tip_mask = ctx.create_mask({3, 5});
        reference::upload(ctx, tip_mask, tip.coverage);
        const std::array brushes{Brush{.diameter = 9, .hardness = 0.4f, .spacing = 0.2f},
                                 Brush{.diameter = 12,
                                       .roundness = 0.5f,
                                       .angle = 20,
                                       .spacing = 0.3f,
                                       .minimum_size = 0.5f,
                                       .minimum_flow = 0.3f},
                                 Brush{.diameter = 8, .spacing = 0.25f, .tip = &tip_mask}};
        for (std::size_t b = 0; b < brushes.size(); ++b) {
            const auto& brush = brushes[b];
            const auto plan = smudge_stroke_requirements({1, 1}, {.brush = brush}).workspace;
            auto scratch = ctx.create_workspace(plan);
            for (float strength : {0.35f, 1.0f}) {
                for (int variant = 0; variant < 3; ++variant) {
                    const bool finger = variant == 1;
                    const reference::Selection selection{
                        variant == 2 ? std::span<const std::uint8_t>(mask_bytes)
                                     : std::span<const std::uint8_t>{},
                        variant == 2 ? std::optional<Rect>{Rect{4, 8, 25, 20}} : std::nullopt};
                    reference::upload(ctx, image, background);
                    ctx.run_and_wait([&](Commands& cmd) {
                        cmd.smudge_stroke(image, {.samples = path,
                                                  .brush = brush,
                                                  .strength = strength,
                                                  .finger_painting = finger,
                                                  .color = {0.5f, 0.1f, 0.05f, 0.8f},
                                                  .mask = variant == 2 ? &mask : nullptr,
                                                  .region = selection.region,
                                                  .workspace = scratch});
                    });
                    const auto expected = smudge(
                        background, path, brush, strength,
                        finger ? std::optional<Pixel>{Pixel{0.5, 0.1, 0.05, 0.8}} : std::nullopt,
                        selection, b == 2 ? &tip : nullptr);
                    reference::expect(reference::download(ctx, image), expected, 1e-5);
                }
            }
            ctx.destroy(scratch);
        }
    });

    test::run("smudge drags color in stroke order and strength 0 changes nothing", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({30, 10});
        auto scratch = ctx.create_workspace(focus_stroke_requirements({64, 64}).workspace);
        const std::array right{StrokeSample{{5, 5}}, StrokeSample{{25, 5}}};
        const std::array left{StrokeSample{{25, 5}}, StrokeSample{{5, 5}}};
        const auto run = [&](std::span<const StrokeSample> samples, float strength) {
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.fill(image, {.color = {0, 0, 1, 1}});
                cmd.fill(image, {.color = {1, 0, 0, 1}, .region = Rect{0, 0, 8, 10}});
                cmd.smudge_stroke(image, {.samples = samples,
                                          .brush = {.diameter = 6, .spacing = 0.1f},
                                          .strength = strength,
                                          .workspace = scratch});
            });
            return reference::download(ctx, image);
        };
        auto result = run(right, 0.9f);
        test::check(result.at(15, 5)[0] > 0.3, "dragging right carries red into blue");
        result = run(left, 0.9f);
        test::check(result.at(15, 5)[0] < 1e-6, "dragging left carries blue into red");
        test::check(result.at(6, 5)[2] > 0.3, "blue reaches the red side");
        result = run(right, 0);
        test::check(result.at(15, 5)[0] == 0 && result.at(6, 5)[0] == 1, "strength 0");
    });

    test::run("smudge validates its scratch and records nothing for empty strokes", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({16, 16});
        auto scratch = ctx.create_workspace(focus_stroke_requirements({32, 32}).workspace);
        auto small = ctx.create_workspace(focus_stroke_requirements({24, 23}).workspace);
        const std::array tap{StrokeSample{{8, 8}}, StrokeSample{{9, 8}}};
        auto cmd = ctx.create_commands(1);
        test::error(ErrorCode::capacity, "smudge_stroke", "workspace", [&] {
            cmd.smudge_stroke(image,
                              {.samples = tap, .brush = {.diameter = 20}, .workspace = small});
        });
        test::error(ErrorCode::capacity, "smudge_stroke", "workspace",
                    [&] { cmd.smudge_stroke(image, {.samples = tap, .workspace = Workspace{}}); });
        test::error(ErrorCode::invalid_argument, "smudge_stroke", "region", [&] {
            cmd.smudge_stroke(image,
                              {.samples = tap, .region = Rect{0, 0, 1, -1}, .workspace = scratch});
        });
        test::error(ErrorCode::invalid_argument, "smudge_stroke", "strength", [&] {
            cmd.smudge_stroke(image, {.samples = tap, .strength = -1, .workspace = scratch});
        });
        cmd.smudge_stroke(image, {.samples = std::span(tap).first(1), .workspace = scratch});
        cmd.smudge_stroke(image,
                          {.samples = tap, .region = Rect{0, 0, 0, 16}, .workspace = scratch});
        const std::array far{StrokeSample{{-80, 8}}, StrokeSample{{-60, 8}}};
        cmd.smudge_stroke(image, {.samples = far, .workspace = scratch});
        cmd.smudge_stroke(image, {.samples = tap, .brush = {.diameter = 20}, .workspace = scratch});
        ctx.submit_and_wait(cmd);
    });
    return test::finish();
}
