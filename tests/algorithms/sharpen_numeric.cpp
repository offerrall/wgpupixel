#include "../test.h"
#include <array>
using namespace wgpupixel;

int main() {
    test::run("sharpen large strength preserves constants, alpha, and clamp edges", [] {
        auto ctx = Context::create();
        for (const auto size :
             {std::array{1, 1}, std::array{1, 5}, std::array{5, 1}, std::array{3, 5}}) {
            auto image = ctx.create_image({size[0], size[1]}),
                 output = ctx.create_image({size[0], size[1]});
            auto commands = ctx.create_commands(2);
            for (float strength : {1e8f, 1e20f, 1e36f}) {
                for (float alpha : {.5f, 1.f}) {
                    commands.fill(image, {.color = {.1f * alpha, .2f * alpha, .3f * alpha, alpha}});
                    commands.sharpen(image, output, {.strength = strength});
                    ctx.submit_and_wait(commands);
                    const auto actual = test::read(ctx, output);
                    const std::array<float, 4> linear{.1f * alpha, .2f * alpha, .3f * alpha, alpha};
                    const auto expected = test::encode(linear);
                    for (std::size_t i = 0; i < actual.size(); ++i) {
                        test::near(actual[i], expected[i % 4], 1);
                    }
                }
            }
        }
    });
    test::run("sharpen weights HDR differences before summing", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 3}), output = ctx.create_image({3, 3}),
             canvas = ctx.create_image({3, 3});
        std::vector<float> pixels;
        for (int i = 0; i < 9; ++i) {
            const float value = i == 4 ? 1e38f : -1e38f;
            pixels.insert(pixels.end(), {value, value, value, 1});
        }
        test::paint(ctx, image, pixels);
        auto commands = ctx.create_commands(5);
        commands.sharpen(image, output, {.strength = .1f});
        commands.opacity(output, {.factor = 1e-37f});
        commands.fill(canvas, {.color = {0, 0, 0, 1}});
        commands.blend(output, canvas, {.position = {0, 0}});
        commands.brightness(canvas, {.amount = -17.5f});
        ctx.submit_and_wait(commands);
        const auto actual = test::read(ctx, canvas);
        for (int channel = 0; channel < 3; ++channel) {
            test::near(actual[16 + channel], test::channel(.5f), 2);
        }
    });
    return test::finish();
}
