#include "../test.h"
#include <array>

using namespace wgpupixel;

namespace {
void expect(Context& ctx, const Image& image, std::span<const float> expected) {
    const auto actual = test::read(ctx, image);
    const auto encoded = test::encode(expected);
    for (std::size_t i = 0; i < actual.size(); ++i) {
        test::check(std::abs(int(actual[i]) - int(encoded[i])) <= 1,
                    "incorrect invert component " + std::to_string(i));
    }
}
} // namespace

int main() {
    test::run("invert complements straight linear RGB and preserves alpha", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 1});
        const std::array<float, 12> input{0.1f, 0.2f, 0.4f, 0.5f, 0.8f, 0.3f,
                                          0.1f, 1.0f, 0,    0,    0,    0};
        test::paint(ctx, image, input);
        auto cmd = ctx.create_commands(1);
        cmd.invert(image);
        ctx.submit_and_wait(cmd);
        const std::array<float, 12> expected{0.4f, 0.3f, 0.1f, 0.5f, 0.2f, 0.7f,
                                             0.9f, 1.0f, 0,    0,    0,    0};
        expect(ctx, image, expected);
        cmd.invert(image);
        ctx.submit_and_wait(cmd);
        expect(ctx, image, input);
    });
    test::run("double inversion preserves HDR and negative RGB", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        const std::array<float, 4> input{-0.125f, 0.75f, 0.2f, 0.5f};
        test::paint(ctx, image, input);
        auto cmd = ctx.create_commands(3);
        cmd.invert(image);
        cmd.invert(image);
        cmd.brightness(image, {.amount = 0.5f});
        ctx.submit_and_wait(cmd);
        const std::array<float, 4> shifted_up{0.125f, 1.0f, 0.45f, 0.5f};
        expect(ctx, image, shifted_up);
        cmd.brightness(image, {.amount = -1.0f});
        ctx.submit_and_wait(cmd);
        const std::array<float, 4> shifted_down{-0.375f, 0.5f, -0.05f, 0.5f};
        expect(ctx, image, shifted_down);
    });
    return test::finish();
}
