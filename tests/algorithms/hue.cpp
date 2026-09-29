#include "../test.h"
#include <array>
#include <limits>

using namespace wgpupixel;

namespace {
void expect(Context& ctx, const Image& image, std::span<const float> expected) {
    const auto actual = test::read(ctx, image);
    const auto encoded = test::encode(expected);
    for (std::size_t i = 0; i < actual.size(); ++i) {
        test::check(std::abs(int(actual[i]) - int(encoded[i])) <= 1,
                    "incorrect hue component " + std::to_string(i));
    }
}
} // namespace

int main() {
    test::run("hue rotates linear primaries with original alpha", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 1});
        const std::array<float, 12> input{0.5f, 0, 0, 0.5f, 0.2f, 0.2f, 0.2f, 0.5f, 0, 0, 0, 0};
        for (float degrees : {120.0f, 480.0f, -240.0f}) {
            test::paint(ctx, image, input);
            auto cmd = ctx.create_commands(1);
            cmd.hue(image, {.degrees = degrees});
            ctx.submit_and_wait(cmd);
            const std::array<float, 12> green{0, 0.5f, 0, 0.5f, 0.2f, 0.2f, 0.2f, 0.5f, 0, 0, 0, 0};
            expect(ctx, image, green);
        }
        test::paint(ctx, image, input);
        auto cmd = ctx.create_commands(1);
        cmd.hue(image, {.degrees = 240.0f});
        ctx.submit_and_wait(cmd);
        const std::array<float, 12> blue{0, 0, 0.5f, 0.5f, 0.2f, 0.2f, 0.2f, 0.5f, 0, 0, 0, 0};
        expect(ctx, image, blue);
    });
    test::run("hue roundtrip preserves negative and HDR RGB", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        const std::array<float, 4> input{-0.125f, 0.625f, 0.2f, 0.5f};
        test::paint(ctx, image, input);
        auto cmd = ctx.create_commands(3);
        cmd.hue(image, {.degrees = 73.0f});
        cmd.hue(image, {.degrees = -73.0f});
        cmd.brightness(image, {.amount = 0.5f});
        ctx.submit_and_wait(cmd);
        const std::array<float, 4> shifted_up{0.125f, 0.875f, 0.45f, 0.5f};
        expect(ctx, image, shifted_up);
        cmd.brightness(image, {.amount = -1.0f});
        ctx.submit_and_wait(cmd);
        const std::array<float, 4> shifted_down{-0.375f, 0.375f, -0.05f, 0.5f};
        expect(ctx, image, shifted_down);
    });
    test::run("hue rejects nonfinite angles before appending", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        auto cmd = ctx.create_commands(1);
        for (float invalid :
             {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            test::error(ErrorCode::invalid_argument, "hue", "degrees",
                        [&] { cmd.hue(image, {.degrees = invalid}); });
        }
        cmd.fill(image, {.color = {0.1f, 0.2f, 0.3f, 0.5f}});
        ctx.submit_and_wait(cmd);
        const std::array<float, 4> expected{0.1f, 0.2f, 0.3f, 0.5f};
        expect(ctx, image, expected);
    });
    return test::finish();
}
