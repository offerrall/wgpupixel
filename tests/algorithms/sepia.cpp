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
                    "incorrect sepia component " + std::to_string(i));
    }
}
} // namespace

int main() {
    test::run("sepia matrix and intensity preserve premultiplied alpha", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 1});
        const std::array<float, 12> input{0.5f, 0, 0, 0.5f, 0.2f, 0.6f, 0.8f, 1.0f, 0, 0, 0, 0};
        const std::array<float, 12> full{0.1965f, 0.1745f, 0.136f, 0.5f, 0.6912f, 0.6158f,
                                         0.4796f, 1.0f,    0,      0,    0,       0};
        for (float intensity : {0.0f, 0.5f, 1.0f}) {
            test::paint(ctx, image, input);
            auto cmd = ctx.create_commands(1);
            cmd.sepia(image, {.intensity = intensity});
            ctx.submit_and_wait(cmd);
            auto expected = input;
            for (std::size_t i = 0; i < expected.size(); ++i) {
                expected[i] += intensity * (full[i] - input[i]);
            }
            expect(ctx, image, expected);
        }
    });
    test::run("sepia preserves HDR and negative results", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        for (float sign : {-1.0f, 1.0f}) {
            const std::array<float, 4> input{sign, sign, sign, 0.5f};
            test::paint(ctx, image, input);
            auto cmd = ctx.create_commands(2);
            cmd.sepia(image);
            const float offset = sign > 0.0f ? -1.8f : 2.8f;
            cmd.brightness(image, {.amount = offset});
            ctx.submit_and_wait(cmd);
            const std::array<float, 4> expected{sign * 1.351f + offset * 0.5f,
                                                sign * 1.203f + offset * 0.5f,
                                                sign * 0.937f + offset * 0.5f, 0.5f};
            expect(ctx, image, expected);
        }
    });
    test::run("sepia validates intensity before appending", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        auto cmd = ctx.create_commands(1);
        for (float invalid : {-0.01f, 1.01f, std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
            test::error(ErrorCode::invalid_argument, "sepia", "intensity",
                        [&] { cmd.sepia(image, {.intensity = invalid}); });
        }
        cmd.fill(image, {.color = {0.1f, 0.2f, 0.3f, 0.5f}});
        ctx.submit_and_wait(cmd);
        const std::array<float, 4> expected{0.1f, 0.2f, 0.3f, 0.5f};
        expect(ctx, image, expected);
    });
    return test::finish();
}
