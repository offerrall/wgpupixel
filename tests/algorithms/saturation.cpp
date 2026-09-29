#include "../test.h"
#include <array>
#include <limits>

using namespace wgpupixel;

namespace {
void expect(Context& ctx, const Image& image, std::span<const float> expected) {
    const auto actual = test::read(ctx, image);
    const auto encoded = test::encode(expected);
    test::check(actual.size() == encoded.size(), "wrong result dimensions");
    for (std::size_t i = 0; i < actual.size(); ++i) {
        test::check(std::abs(int(actual[i]) - int(encoded[i])) <= 1,
                    "incorrect saturation component " + std::to_string(i));
    }
}
} // namespace

int main() {
    test::run("saturation uses linear Rec709 luminance and preserves alpha", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 1});
        const std::array<float, 12> input{0.45f, 0.05f, 0.1f, 0.5f, 0.2f, 0.7f,
                                          0.3f,  1.0f,  0,    0,    0,    0};
        for (float factor : {0.0f, 0.5f, 1.0f, 2.0f}) {
            test::paint(ctx, image, input);
            auto cmd = ctx.create_commands(1);
            cmd.saturation(image, {.factor = factor});
            ctx.submit_and_wait(cmd);
            auto expected = input;
            for (std::size_t i = 0; i < expected.size(); i += 4) {
                const float luma =
                    input[i] * 0.2126f + input[i + 1] * 0.7152f + input[i + 2] * 0.0722f;
                for (std::size_t c = 0; c < 3; ++c) {
                    expected[i + c] = luma + factor * (input[i + c] - luma);
                }
            }
            expect(ctx, image, expected);
        }
        test::paint(ctx, image, input);
        auto cmd = ctx.create_commands(2);
        cmd.saturation(image, {.factor = 4.0f});
        cmd.saturation(image, {.factor = 0.25f});
        ctx.submit_and_wait(cmd);
        expect(ctx, image, input); // Inverting oversaturation detects clipping of HDR/negative RGB.
    });
    test::run("saturation validates factors before appending commands", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        auto cmd = ctx.create_commands(1);
        for (float invalid : {-1.0f, std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
            test::error(ErrorCode::invalid_argument, "saturation", "factor",
                        [&] { cmd.saturation(image, {.factor = invalid}); });
        }
        cmd.fill(image, {.color = {0.1f, 0.2f, 0.3f, 0.5f}});
        ctx.submit_and_wait(cmd);
        const std::array<float, 4> expected{0.1f, 0.2f, 0.3f, 0.5f};
        expect(ctx, image, expected);
    });
    return test::finish();
}
