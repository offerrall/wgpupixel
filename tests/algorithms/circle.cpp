#include "../test.h"
#include <array>
#include <limits>

using namespace wgpupixel;

int main() {
    test::run("circle centered coverage, nonsquare and degenerate dimensions", [] {
        auto ctx = Context::create();
        for (auto size : {std::array{7, 5}, std::array{4, 6}, std::array{1, 5}, std::array{5, 1},
                          std::array{1, 1}}) {
            auto image = ctx.create_image({size[0], size[1]});
            for (float softness : {0.0f, 3.0f}) {
                auto commands = ctx.create_commands(1);
                commands.circle(image, {.color = {0.125f, 0, 0, 0.25f},
                                        .background = {0, 0.25f, 0.5f, 0.5f},
                                        .softness = softness});
                ctx.submit_and_wait(commands);
                std::vector<float> expected;
                for (int y = 0; y < size[1]; ++y) {
                    for (int x = 0; x < size[0]; ++x) {
                        const float distance =
                            std::hypot(x + 0.5f - size[0] * 0.5f, y + 0.5f - size[1] * 0.5f);
                        const float radius = std::min(size[0], size[1]) * 0.5f;
                        const float t = std::clamp(
                            0.5f + (radius - distance) / std::max(1.0f, softness), 0.0f, 1.0f);
                        const float coverage = t * t * (3 - 2 * t);
                        expected.insert(expected.end(),
                                        {0.125f * coverage, 0.25f * (1 - coverage),
                                         0.5f * (1 - coverage), 0.5f - 0.25f * coverage});
                    }
                }
                const auto encoded = test::encode(expected);
                const auto actual = test::read(ctx, image);
                for (std::size_t i = 0; i < encoded.size(); ++i) {
                    test::check(std::abs(int(actual[i]) - int(encoded[i])) <= 1,
                                "circle differs from CPU coverage");
                }
                for (int y = 0; y < size[1]; ++y) {
                    for (int x = 0; x < size[0]; ++x) {
                        for (int c = 0; c < 4; ++c) {
                            test::check(actual[4 * (y * size[0] + x) + c] ==
                                            actual[4 * (y * size[0] + size[0] - 1 - x) + c],
                                        "circle is not horizontally symmetric");
                        }
                    }
                }
            }
        }
    });
    test::run("circle HDR and atomic validation", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        auto commands = ctx.create_commands(2);
        for (float invalid : {-1.0f, std::numeric_limits<float>::infinity()}) {
            test::error(ErrorCode::invalid_argument, "circle", "softness", [&] {
                commands.circle(
                    image,
                    {.color = {0, 0, 0, 1}, .background = {0, 0, 0, 1}, .softness = invalid});
            });
        }
        test::error(ErrorCode::invalid_argument, "circle", "color", [&] {
            commands.circle(image, {.color = {0, 0, 0, 2}, .background = {0, 0, 0, 1}});
        });
        commands.circle(image, {.color = {0.75f, 0.75f, 0.75f, 0.5f}, .background = {0, 0, 0, 0}});
        commands.brightness(image, {.amount = -1.0f});
        ctx.submit_and_wait(commands);
        const auto actual = test::read(ctx, image);
        test::near(actual[0], test::channel(0.5f), 1);
        test::near(actual[3], 128, 1);
    });
    return test::finish();
}
