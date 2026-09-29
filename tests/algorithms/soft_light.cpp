#include "../test.h"
#include <array>

using namespace wgpupixel;

namespace {
using Pixel = std::array<float, 4>;

void expect(Context& ctx, const Image& image, std::span<const float> expected) {
    const auto actual = test::read(ctx, image);
    const auto encoded = test::encode(expected);
    for (std::size_t i = 0; i < actual.size(); ++i) {
        test::check(std::abs(int(actual[i]) - int(encoded[i])) <= 1,
                    "incorrect soft light component " + std::to_string(i));
    }
}

Pixel compose(Pixel source, Pixel destination, float opacity) {
    const double source_alpha = source[3] * opacity;
    const double dest_alpha = destination[3];
    Pixel result{};
    result[3] = static_cast<float>(source_alpha + dest_alpha * (1 - source_alpha));
    for (std::size_t c = 0; c < 3; ++c) {
        const double foreground = source[3] == 0 ? 0 : source[c] / source[3];
        const double backdrop = dest_alpha == 0 ? 0 : destination[c] / dest_alpha;
        const double d = backdrop <= .25 ? ((16 * backdrop - 12) * backdrop + 4) * backdrop
                                         : std::sqrt(backdrop);
        const double overlap = foreground <= .5
                                   ? backdrop - (1 - 2 * foreground) * backdrop * (1 - backdrop)
                                   : backdrop + (2 * foreground - 1) * (d - backdrop);
        result[c] = static_cast<float>(source_alpha * (1 - dest_alpha) * foreground +
                                       dest_alpha * (1 - source_alpha) * backdrop +
                                       source_alpha * dest_alpha * overlap);
    }
    return result;
}
} // namespace

int main() {
    test::run("soft light uses W3C piecewise formula with source-over coverage", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({1, 1});
        auto destination = ctx.create_image({1, 1});
        for (const Pixel foreground :
             {Pixel{0.1f, 0.25f, 0.45f, 0.5f}, Pixel{0.8f, 0.1f, 0.5f, 1.0f}, Pixel{0, 0, 0, 0}}) {
            for (const Pixel backdrop : {Pixel{0.05f, 0.1f, 0.2f, 0.25f},
                                         Pixel{0.25f, 0.5f, 0.75f, 1.0f}, Pixel{0, 0, 0, 0}}) {
                for (float opacity : {0.0f, 0.4f, 1.0f}) {
                    test::paint(ctx, source, foreground);
                    test::paint(ctx, destination, backdrop);
                    auto cmd = ctx.create_commands(1);
                    cmd.blend(source, destination,
                              {.position = {}, .opacity = opacity, .mode = BlendMode::soft_light});
                    ctx.submit_and_wait(cmd);
                    expect(ctx, destination, compose(foreground, backdrop, opacity));
                }
            }
        }
    });
    test::run("soft light preserves unclamped signed and HDR output", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({1, 1});
        auto destination = ctx.create_image({1, 1});
        // Black foreground squares straight backdrop channels, including HDR.
        const Pixel foreground{0, 0, 0, 1};
        const Pixel backdrop{1.2f, 1.3f, 1.4f, 1.0f};
        test::paint(ctx, source, foreground);
        test::paint(ctx, destination, backdrop);
        auto cmd = ctx.create_commands(2);
        cmd.blend(source, destination,
                  {.position = {}, .opacity = 1.0f, .mode = BlendMode::soft_light});
        cmd.brightness(destination, {.amount = -1.0f});
        ctx.submit_and_wait(cmd);
        auto expected = compose(foreground, backdrop, 1.0f);
        for (std::size_t c = 0; c < 3; ++c) {
            expected[c] -= expected[3];
        }
        expect(ctx, destination, expected);

        const Pixel negative_foreground{-0.5f, -0.4f, -0.3f, 0.5f};
        const Pixel neutral_backdrop{0.25f, 0.25f, 0.25f, 0.5f};
        test::paint(ctx, source, negative_foreground);
        test::paint(ctx, destination, neutral_backdrop);
        cmd.blend(source, destination,
                  {.position = {}, .opacity = 1.0f, .mode = BlendMode::soft_light});
        cmd.brightness(destination, {.amount = 0.5f});
        ctx.submit_and_wait(cmd);
        expected = compose(negative_foreground, neutral_backdrop, 1.0f);
        for (std::size_t c = 0; c < 3; ++c) {
            expected[c] += 0.5f * expected[3];
        }
        expect(ctx, destination, expected);
    });
    return test::finish();
}
