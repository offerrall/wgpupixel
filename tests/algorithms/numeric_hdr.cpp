#include "../test.h"
#include <array>
#include <cmath>

using namespace wgpupixel;

namespace {
void expect(Context& ctx, const Image& image, const std::array<float, 4>& expected) {
    const auto actual = test::read(ctx, image);
    const auto encoded = test::encode(expected);
    for (std::size_t i = 0; i < 4; ++i) {
        test::check(std::abs(int(actual[i]) - int(encoded[i])) <= 2,
                    "HDR diagnostic differs at channel " + std::to_string(i));
    }
}
} // namespace

int main() {
    test::run("multiply screen and exclusion preserve finite overlap with tiny alpha", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({1, 1});
        auto destination = ctx.create_image({1, 1});
        auto visible = ctx.create_image({1, 1});
        for (auto mode : {BlendMode::multiply, BlendMode::screen, BlendMode::exclusion}) {
            for (float opacity : {0.25f, 1.0f}) {
                auto cmd = ctx.create_commands(6);
                cmd.fill(source, {.color = {1, 1, 1, 1e-20f}});
                cmd.fill(destination, {.color = {1, 1, 1, 1e-20f}});
                cmd.fill(visible, {.color = {0, 0, 0, 1}});
                cmd.blend(source, destination, {.position = {}, .opacity = opacity, .mode = mode});
                cmd.blend(destination, visible, {.position = {}});
                // Independent real arithmetic simplifies to these values for
                // B=P=1. Bring the result into range; NaN/Inf cannot pass this.
                const float expected = mode == BlendMode::multiply ? 1 + 2 * opacity
                                       : mode == BlendMode::screen ? 1
                                                                   : 1 - opacity;
                cmd.brightness(visible, {.amount = 0.5f - expected});
                ctx.submit_and_wait(cmd);
                expect(ctx, visible, {0.5f, 0.5f, 0.5f, 1});
            }
        }
    });
    test::run("hue and vibrance retain HDR exceeding straight float32", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        auto scaled = ctx.create_image({1, 1});
        auto visible = ctx.create_image({1, 1});
        for (bool vibrance : {false, true}) {
            auto cmd = ctx.create_commands(7);
            cmd.fill(image, {.color = {1e20f, 5e19f, 2.5e19f, 1e-20f}});
            cmd.fill(scaled, {.color = {0, 0, 0, 1}});
            cmd.fill(visible, {.color = {0, 0, 0, 1}});
            if (vibrance) {
                cmd.vibrance(image, {.amount = 1});
            } else {
                cmd.hue(image, {.degrees = 60});
            }
            // Make alpha opaque before rescaling to avoid subnormal alpha.
            cmd.blend(image, scaled, {.position = {}});
            cmd.opacity(scaled, {.factor = 1e-20f});
            cmd.blend(scaled, visible, {.position = {}});
            ctx.submit_and_wait(cmd);
            // Hue: input angle 20 -> 80 degrees. Vibrance: saturation .75,
            // protected hue, factor 1 + .25*.3 = 1.075.
            expect(ctx, visible,
                   vibrance ? std::array<float, 4>{1, .4625f, .19375f, 1}
                            : std::array<float, 4>{.75f, 1, .25f, 1});
        }
    });
    test::run("desaturation remains finite when RGB minus luma overflows", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        auto cmd = ctx.create_commands(4);
        cmd.fill(image, {.color = {-3e38f, 3e38f, 3e38f, 1}});
        cmd.saturation(image, {.factor = 0});
        // Compress positive HDR to visible values without a subnormal factor.
        cmd.gamma(image, {.value = 100});
        cmd.brightness(image, {.amount = -2});
        ctx.submit_and_wait(cmd);
        const auto gray = float(std::pow(3e38 * (-.2126 + .7152 + .0722), .01) - 2);
        expect(ctx, image, {gray, gray, gray, 1});
    });
    return test::finish();
}
