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
                    "incorrect threshold component " + std::to_string(i));
    }
}
} // namespace

int main() {
    test::run("threshold compares Rec709 straight luminance preserving alpha", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({5, 1});
        const std::array<float, 20> input{0.5f,   0,     0,     0.5f,  0,    0.5f,   0,
                                          0.5f,   0.25f, 0.25f, 0.25f, 0.5f, 0.249f, 0.249f,
                                          0.249f, 0.5f,  0,     0,     0,    0};
        test::paint(ctx, image, input);
        auto cmd = ctx.create_commands(1);
        cmd.threshold(image);
        ctx.submit_and_wait(cmd);
        const std::array<float, 20> expected{0,    0,    0,    0.5f, 0.5f, 0.5f, 0.5f,
                                             0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0,    0,
                                             0,    0.5f, 0,    0,    0,    0};
        expect(ctx, image, expected);
    });
    test::run("threshold evaluates HDR and negative RGB without clipping", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({2, 1});
        const std::array<float, 8> input{2.0f, 0, 0, 0.5f, -2.0f, 0.5f, 0, 0.5f};
        test::paint(ctx, image, input);
        auto cmd = ctx.create_commands(1);
        cmd.threshold(image, {.value = 0.5f});
        ctx.submit_and_wait(cmd);
        const std::array<float, 8> expected{0.5f, 0.5f, 0.5f, 0.5f, 0, 0, 0, 0.5f};
        expect(ctx, image, expected);
    });
    test::run("threshold zero includes opaque black but preserves transparency", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({2, 1});
        const std::array<float, 8> input{0, 0, 0, 1, 0, 0, 0, 0};
        test::paint(ctx, image, input);
        auto cmd = ctx.create_commands(1);
        cmd.threshold(image, {.value = 0.0f});
        ctx.submit_and_wait(cmd);
        const std::array<float, 8> expected{1, 1, 1, 1, 0, 0, 0, 0};
        expect(ctx, image, expected);
    });
    test::run("threshold validates range before appending", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        auto cmd = ctx.create_commands(1);
        for (float invalid : {-0.01f, 1.01f, std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
            test::error(ErrorCode::invalid_argument, "threshold", "value",
                        [&] { cmd.threshold(image, {.value = invalid}); });
        }
        cmd.fill(image, {.color = {0.1f, 0.2f, 0.3f, 0.5f}});
        ctx.submit_and_wait(cmd);
        const std::array<float, 4> expected{0.1f, 0.2f, 0.3f, 0.5f};
        expect(ctx, image, expected);
    });
    return test::finish();
}
