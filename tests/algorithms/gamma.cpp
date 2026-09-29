#include "../test.h"
#include <array>
#include <limits>

using namespace wgpupixel;

namespace {
void expect(Context& ctx, const Image& image, std::span<const float> expected) {
    const auto actual = test::read_float(ctx, image);
    for (std::size_t i = 0; i < actual.size(); ++i) {
        test::near(actual[i], expected[i]);
    }
}
} // namespace

int main() {
    test::run("gamma operates on straight linear RGB and preserves coverage", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 1});
        const std::array<float, 12> input{0.125f, 0.32f, 0, 0.5f, 0.25f, 0.64f,
                                          1.0f,   1.0f,  0, 0,    0,     0};
        test::paint(ctx, image, input);
        auto cmd = ctx.create_commands(1);
        cmd.gamma(image, {.value = 2.0f});
        ctx.submit_and_wait(cmd);
        const std::array<float, 12> expected{0.25f, 0.4f, 0, 0.5f, 0.5f, 0.8f,
                                             1.0f,  1.0f, 0, 0,    0,    0};
        expect(ctx, image, expected);
        cmd.gamma(image, {.value = 0.5f});
        ctx.submit_and_wait(cmd);
        expect(ctx, image, input);
    });
    test::run("signed gamma preserves negative and HDR RGB", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        const std::array<float, 4> negative{-0.125f, -0.32f, 0, 0.5f};
        test::paint(ctx, image, negative);
        auto cmd = ctx.create_commands(3);
        cmd.gamma(image, {.value = 2.0f});
        cmd.gamma(image, {.value = 0.5f});
        cmd.brightness(image, {.amount = 1.0f});
        ctx.submit_and_wait(cmd);
        const std::array<float, 4> recovered{0.375f, 0.18f, 0.5f, 0.5f};
        expect(ctx, image, recovered);
        const std::array<float, 4> hdr{2.0f, 1.125f, 0.5f, 0.5f};
        test::paint(ctx, image, hdr);
        cmd.gamma(image, {.value = 2.0f});
        cmd.brightness(image, {.amount = -1.0f});
        ctx.submit_and_wait(cmd);
        const std::array<float, 4> mapped{0.5f, 0.25f, 0, 0.5f};
        expect(ctx, image, mapped);
    });
    test::run("gamma square roots retain signed HDR with masks regions and small alpha", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({17, 9});
        auto mask = ctx.create_mask(image.size());
        std::vector<float> input(17 * 9 * 4), expected(input.size());
        for (int y = 0; y < 9; ++y) {
            for (int x = 0; x < 17; ++x) {
                const float alpha = std::array{0.0f, 0.5f, 1.0f, 1e-4f}[x % 4];
                const std::array before{-4 * alpha, alpha, 16 * alpha, alpha};
                // Exact signed square roots of straight RGB {-4,1,16}.
                const std::array after{-2 * alpha, alpha, 4 * alpha, alpha};
                const float coverage = std::array{0.0f, 128.0f / 255.0f, 1.0f}[y % 3];
                const float weight = x >= 1 && x < 16 && y >= 1 && y < 8 ? coverage : 0;
                for (int c = 0; c < 4; ++c) {
                    const auto i = (y * 17 + x) * 4 + c;
                    input[i] = before[c];
                    expected[i] = before[c] + (after[c] - before[c]) * weight;
                }
            }
        }
        test::paint(ctx, image, input);
        ctx.run_and_wait([&](Commands& cmd) {
            for (int y = 0; y < 9; ++y) {
                cmd.fill(mask, {.coverage = std::array{0.0f, 128.0f / 255.0f, 1.0f}[y % 3],
                                .region = Rect{0, y, 17, 1}});
            }
            cmd.gamma(image, {.value = 2, .mask = &mask, .region = Rect{1, 1, 15, 7}});
        });
        expect(ctx, image, expected);
    });
    test::run("gamma validates positive finite reciprocal before appending", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        auto cmd = ctx.create_commands(1);
        for (float invalid :
             {0.0f, -1.0f, std::numeric_limits<float>::denorm_min(),
              std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            test::error(ErrorCode::invalid_argument, "gamma", "value",
                        [&] { cmd.gamma(image, {.value = invalid}); });
        }
        cmd.fill(image, {.color = {0.1f, 0.2f, 0.3f, 0.5f}});
        ctx.submit_and_wait(cmd);
        const std::array<float, 4> expected{0.1f, 0.2f, 0.3f, 0.5f};
        expect(ctx, image, expected);
    });
    return test::finish();
}
