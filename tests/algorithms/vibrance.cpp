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
                    "incorrect vibrance component " + std::to_string(i));
    }
}
} // namespace

int main() {
    test::run("vibrance protects warm hues and respects existing saturation", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({5, 1});
        const std::array<float, 20> input{0.25f, 0.5f,  0.25f, 0.5f,  0.5f, 0.25f, 0.25f,
                                          0.5f,  0.25f, 0.25f, 0.25f, 0.5f, 0.5f,  0,
                                          0,     0.5f,  0,     0,     0,    0};
        for (float amount : {-1.0f, 0.0f, 1.0f}) {
            test::paint(ctx, image, input);
            auto cmd = ctx.create_commands(1);
            cmd.vibrance(image, {.amount = amount});
            ctx.submit_and_wait(cmd);
            auto expected = input;
            expected[0] = expected[2] = 0.25f - 0.125f * amount;
            expected[5] = expected[6] = 0.25f - 0.0375f * amount;
            expect(ctx, image, expected);
        }
    });
    test::run("vibrance retains HDR and leaves signed colors unchanged", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        const std::array<float, 4> hdr{0.5f, 1.0f, 0.5f, 0.5f};
        test::paint(ctx, image, hdr);
        auto cmd = ctx.create_commands(2);
        cmd.vibrance(image, {.amount = 1.0f});
        cmd.brightness(image, {.amount = -1.0f});
        ctx.submit_and_wait(cmd);
        const std::array<float, 4> expected_hdr{-0.25f, 0.5f, -0.25f, 0.5f};
        expect(ctx, image, expected_hdr);
        const std::array<float, 4> negative{-0.25f, -0.125f, 0, 0.5f};
        test::paint(ctx, image, negative);
        cmd.vibrance(image, {.amount = 1.0f});
        cmd.brightness(image, {.amount = 0.5f});
        ctx.submit_and_wait(cmd);
        const std::array<float, 4> expected_negative{0, 0.125f, 0.25f, 0.5f};
        expect(ctx, image, expected_negative);
    });
    test::run("vibrance validates amount before appending", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        auto cmd = ctx.create_commands(1);
        for (float invalid : {-1.01f, 1.01f, std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
            test::error(ErrorCode::invalid_argument, "vibrance", "amount",
                        [&] { cmd.vibrance(image, {.amount = invalid}); });
        }
        cmd.fill(image, {.color = {0.1f, 0.2f, 0.3f, 0.5f}});
        ctx.submit_and_wait(cmd);
        const std::array<float, 4> expected{0.1f, 0.2f, 0.3f, 0.5f};
        expect(ctx, image, expected);
    });
    return test::finish();
}
