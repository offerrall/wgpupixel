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
    test::run("exposure scales linear RGB in stops and preserves alpha", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 1});
        const std::array<float, 12> input{0.025f, 0.07f, 0.12f, 0.25f, 0.15f, 0.3f,
                                          0.4f,   1.0f,  0,     0,     0,     0};
        for (float stops : {-2.0f, 0.0f, 0.5f, 1.0f}) {
            test::paint(ctx, image, input);
            auto commands = ctx.create_commands(1);
            commands.exposure(image, {.stops = stops});
            ctx.submit_and_wait(commands);
            auto expected = input;
            for (std::size_t i = 0; i < expected.size(); ++i) {
                if (i % 4 != 3) {
                    expected[i] *= static_cast<float>(std::pow(2.0, stops));
                }
            }
            expect(ctx, image, expected);
        }
    });
    test::run("exposure retains HDR and negative RGB for later operations", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        const std::array<float, 4> input{0.125f, 0.25f, 0.375f, 0.5f};
        test::paint(ctx, image, input);
        auto commands = ctx.create_commands(2);
        commands.exposure(image, {.stops = 5.0f});
        ctx.submit_and_wait(commands);
        const std::array<float, 4> hdr{4, 8, 12, 0.5f};
        expect(ctx, image, hdr);
        commands.exposure(image, {.stops = -5.0f});
        ctx.submit_and_wait(commands);
        expect(ctx, image, input);
        const std::array<float, 4> negative{-0.125f, -0.0625f, 0.0625f, 0.5f};
        test::paint(ctx, image, negative);
        commands.exposure(image, {.stops = 1.0f});
        commands.brightness(image, {.amount = 0.75f});
        ctx.submit_and_wait(commands);
        const std::array<float, 4> recovered{0.125f, 0.25f, 0.5f, 0.5f};
        expect(ctx, image, recovered);
    });
    test::run("exposure interpolates zero fractional and full mask coverage", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        auto mask = ctx.create_mask({1, 1});
        const std::array<float, 4> input{0.04f, 0.08f, 0.12f, 0.5f};
        for (float coverage : {0.0f, 0.5f, 1.0f}) {
            test::paint(ctx, image, input);
            auto commands = ctx.create_commands(2);
            commands.fill(mask, {.coverage = coverage});
            commands.exposure(image, {.stops = 1.0f, .mask = &mask});
            ctx.submit_and_wait(commands);
            auto expected = input;
            // Masks store byte coverage, including fill values.
            const float weight = std::floor(coverage * 255.0f + 0.5f) / 255.0f;
            for (int i = 0; i < 3; ++i) {
                expected[i] *= 1.0f + weight;
            }
            expect(ctx, image, expected);
        }
    });
    test::run("exposure rejects unrepresentable gains without consuming capacity", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        auto commands = ctx.create_commands(1);
        for (float stops :
             {-127.0f, 128.0f, std::numeric_limits<float>::max(),
              -std::numeric_limits<float>::max(), std::numeric_limits<float>::infinity(),
              -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            test::error(ErrorCode::invalid_argument, "exposure", "stops",
                        [&] { commands.exposure(image, {.stops = stops}); });
        }
        commands.fill(image, {.color = {0.125f, 0.25f, 0.375f, 0.5f}});
        ctx.submit_and_wait(commands);
        const std::array<float, 4> expected{0.125f, 0.25f, 0.375f, 0.5f};
        expect(ctx, image, expected);
        // Normal float32 gain boundaries are accepted; zero RGB avoids overflow.
        commands.fill(image, {.color = {0, 0, 0, 0.5f}});
        ctx.submit_and_wait(commands);
        for (float stops : {-126.0f, 127.0f, std::nextafter(128.0f, 0.0f)}) {
            commands.exposure(image, {.stops = stops});
            ctx.submit_and_wait(commands);
            const std::array<float, 4> black{0, 0, 0, 0.5f};
            expect(ctx, image, black);
        }
    });
    return test::finish();
}
