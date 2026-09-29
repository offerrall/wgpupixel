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
                    "incorrect opacity component " + std::to_string(i));
    }
}
} // namespace

int main() {
    test::run("opacity scales coverage and premultiplied RGB together", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 1});
        const std::array<float, 12> input{0.1f, 0.2f, 0.4f, 0.5f, 0.8f, 0.3f,
                                          0.1f, 1.0f, 0,    0,    0,    0};
        for (float factor : {0.0f, 0.25f, 1.0f}) {
            test::paint(ctx, image, input);
            auto cmd = ctx.create_commands(1);
            cmd.opacity(image, {.factor = factor});
            ctx.submit_and_wait(cmd);
            auto expected = input;
            for (auto& channel : expected) {
                channel *= factor;
            }
            expect(ctx, image, expected);
        }
    });
    test::run("opacity retains HDR and negative RGB", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        const std::array<float, 4> hdr{0.75f, 0.875f, 1.0f, 0.5f};
        test::paint(ctx, image, hdr);
        auto cmd = ctx.create_commands(2);
        cmd.opacity(image, {.factor = 0.5f});
        cmd.brightness(image, {.amount = -1.0f});
        ctx.submit_and_wait(cmd);
        const std::array<float, 4> mapped_hdr{0.125f, 0.1875f, 0.25f, 0.25f};
        expect(ctx, image, mapped_hdr);
        const std::array<float, 4> negative{-0.375f, -0.25f, -0.125f, 0.5f};
        test::paint(ctx, image, negative);
        cmd.opacity(image, {.factor = 0.5f});
        cmd.brightness(image, {.amount = 1.0f});
        ctx.submit_and_wait(cmd);
        const std::array<float, 4> mapped_negative{0.0625f, 0.125f, 0.1875f, 0.25f};
        expect(ctx, image, mapped_negative);
    });
    test::run("opacity validates range before appending", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        auto cmd = ctx.create_commands(1);
        for (float invalid : {-1.0f, 1.01f, std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
            test::error(ErrorCode::invalid_argument, "opacity", "factor",
                        [&] { cmd.opacity(image, {.factor = invalid}); });
        }
        cmd.fill(image, {.color = {0.1f, 0.2f, 0.3f, 0.5f}});
        ctx.submit_and_wait(cmd);
        const std::array<float, 4> expected{0.1f, 0.2f, 0.3f, 0.5f};
        expect(ctx, image, expected);
    });
    return test::finish();
}
