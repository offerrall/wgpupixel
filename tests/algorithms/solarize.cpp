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
                    "incorrect solarize component " + std::to_string(i));
    }
}
} // namespace

int main() {
    test::run("solarize compares straight RGB strictly above the threshold", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 1});
        const std::array<float, 12> input{0.1f, 0.125f, 0.4f, 0.5f, 0.25f, 0.3f,
                                          0.8f, 1.0f,   0,    0,    0,     0};
        test::paint(ctx, image, input);
        auto cmd = ctx.create_commands(1);
        cmd.solarize(image, {.value = 0.25f});
        ctx.submit_and_wait(cmd);
        const std::array<float, 12> expected{0.1f, 0.125f, 0.1f, 0.5f, 0.25f, 0.7f,
                                             0.2f, 1.0f,   0,    0,    0,     0};
        expect(ctx, image, expected);
    });
    test::run("solarize retains original negative and transformed HDR channels", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        const std::array<float, 4> input{-0.125f, 0.75f, 0.2f, 0.5f};
        test::paint(ctx, image, input);
        auto cmd = ctx.create_commands(2);
        cmd.solarize(image);
        cmd.brightness(image, {.amount = 1.0f});
        ctx.submit_and_wait(cmd);
        const std::array<float, 4> expected{0.375f, 0.25f, 0.7f, 0.5f};
        expect(ctx, image, expected);
    });
    test::run("solarize validates range before appending", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        auto cmd = ctx.create_commands(1);
        for (float invalid : {-0.01f, 1.01f, std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
            test::error(ErrorCode::invalid_argument, "solarize", "value",
                        [&] { cmd.solarize(image, {.value = invalid}); });
        }
        cmd.fill(image, {.color = {0.1f, 0.2f, 0.3f, 0.5f}});
        ctx.submit_and_wait(cmd);
        const std::array<float, 4> expected{0.1f, 0.2f, 0.3f, 0.5f};
        expect(ctx, image, expected);
    });
    return test::finish();
}
