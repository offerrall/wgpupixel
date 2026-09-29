#include "../test.h"
#include <array>
#include <limits>

using namespace wgpupixel;

int main() {
    test::run("contrast preserves alpha and reversible HDR/negative values", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 1});
        const std::array<float, 12> input{0.05f, 0.3f, 0.45f, 0.5f, 0.1f, 0.6f,
                                          0.9f,  1.0f, 0,     0,    0,    0};
        test::paint(ctx, image, input);
        auto cmd = ctx.create_commands(2);
        cmd.contrast(image, {.factor = 4.0f});
        cmd.contrast(image, {.factor = 0.25f});
        ctx.submit_and_wait(cmd);
        const auto actual = test::read(ctx, image);
        const auto expected = test::encode(input);
        for (std::size_t i = 0; i < actual.size(); ++i) {
            test::check(std::abs(int(actual[i]) - int(expected[i])) <= 1,
                        "contrast lost alpha, HDR, or negative RGB");
        }
        cmd.contrast(image, {.factor = 0.0f});
        ctx.submit_and_wait(cmd);
        const auto neutral = test::read(ctx, image);
        for (std::size_t i = 0; i < 8; ++i) {
            const int expected_channel = i % 4 == 3 ? (i == 3 ? 128 : 255) : test::channel(0.5f);
            test::check(std::abs(int(neutral[i]) - expected_channel) <= 1,
                        "zero contrast must produce linear middle gray preserving alpha");
        }
        for (std::size_t i = 8; i < 12; ++i) {
            test::check(neutral[i] == 0, "transparent pixel must remain zero");
        }
    });
    test::run("contrast invalid factors do not consume command capacity", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        auto cmd = ctx.create_commands(1);
        for (float invalid : {-1.0f, std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
            test::error(ErrorCode::invalid_argument, "contrast", "factor",
                        [&] { cmd.contrast(image, {.factor = invalid}); });
        }
        cmd.fill(image, {.color = {0.1f, 0.2f, 0.3f, 0.5f}});
        ctx.submit_and_wait(cmd);
        const std::array<float, 4> expected{0.1f, 0.2f, 0.3f, 0.5f};
        const auto actual = test::read(ctx, image);
        const auto encoded = test::encode(expected);
        for (std::size_t i = 0; i < 4; ++i) {
            test::check(std::abs(int(actual[i]) - int(encoded[i])) <= 1,
                        "failed contrast altered recorded commands");
        }
    });
    return test::finish();
}
