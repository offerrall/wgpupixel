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
                    "incorrect hard light component " + std::to_string(i));
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
        const double overlap = foreground <= 0.5 ? 2 * backdrop * foreground
                                                 : 1 - 2 * (1 - backdrop) * (1 - foreground);
        result[c] = static_cast<float>(source_alpha * (1 - dest_alpha) * foreground +
                                       dest_alpha * (1 - source_alpha) * backdrop +
                                       source_alpha * dest_alpha * overlap);
    }
    return result;
}
} // namespace

int main() {
    test::run("hard light chooses branch from straight foreground with source-over alpha", [] {
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
                              {.position = {}, .opacity = opacity, .mode = BlendMode::hard_light});
                    ctx.submit_and_wait(cmd);
                    expect(ctx, destination, compose(foreground, backdrop, opacity));
                }
            }
        }
    });
    test::run("hard light preserves negative and HDR results", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({1, 1});
        auto destination = ctx.create_image({1, 1});
        for (const Pixel foreground :
             {Pixel{1.2f, 1.3f, 1.4f, 1.0f}, Pixel{-0.2f, -0.3f, -0.4f, 1.0f}}) {
            const Pixel backdrop{0.5f, 0.6f, 0.7f, 1.0f};
            test::paint(ctx, source, foreground);
            test::paint(ctx, destination, backdrop);
            auto cmd = ctx.create_commands(2);
            cmd.blend(source, destination,
                      {.position = {}, .opacity = 1.0f, .mode = BlendMode::hard_light});
            const float shift = foreground[0] > 0 ? -0.5f : 1.0f;
            cmd.brightness(destination, {.amount = shift});
            ctx.submit_and_wait(cmd);
            auto expected = compose(foreground, backdrop, 1.0f);
            for (std::size_t c = 0; c < 3; ++c) {
                expected[c] += shift * expected[3];
            }
            expect(ctx, destination, expected);
        }
    });
    return test::finish();
}
