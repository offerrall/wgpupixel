#include "../test.h"
#include <limits>

using namespace wgpupixel;

int main() {
    test::run("dots centers, periodicity, soft edges and alpha", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({7, 5});
        for (Position offset : {Position{0, 0}, Position{-4, 4}, Position{-1, 2},
                                Position{std::numeric_limits<std::int32_t>::min(),
                                         std::numeric_limits<std::int32_t>::max()}}) {
            for (float radius : {0.0f, 1.0f, 2.0f}) {
                for (float softness : {0.0f, 3.0f}) {
                    auto commands = ctx.create_commands(1);
                    commands.dots(image, {.spacing = 4,
                                          .radius = radius,
                                          .color = {0.125f, 0, 0, 0.25f},
                                          .background = {0, 0.25f, 0.5f, 0.5f},
                                          .offset = offset,
                                          .softness = softness});
                    ctx.submit_and_wait(commands);
                    std::vector<float> expected;
                    for (int y = 0; y < 5; ++y) {
                        for (int x = 0; x < 7; ++x) {
                            const double dx = std::remainder(double(x) - offset.x, 4.0);
                            const double dy = std::remainder(double(y) - offset.y, 4.0);
                            const double t = std::clamp(0.5 + (radius - std::hypot(dx, dy)) /
                                                                  std::max(1.0f, softness),
                                                        0.0, 1.0);
                            const float coverage = radius == 0 ? 0 : float(t * t * (3 - 2 * t));
                            expected.insert(expected.end(),
                                            {0.125f * coverage, 0.25f * (1 - coverage),
                                             0.5f * (1 - coverage), 0.5f - 0.25f * coverage});
                        }
                    }
                    const auto encoded = test::encode(expected);
                    const auto actual = test::read(ctx, image);
                    for (std::size_t i = 0; i < encoded.size(); ++i) {
                        test::check(std::abs(int(actual[i]) - int(encoded[i])) <= 1,
                                    "dots differ from CPU coverage");
                    }
                }
            }
        }
    });
    test::run("dots HDR and atomic validation", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 1});
        auto commands = ctx.create_commands(2);
        test::error(ErrorCode::invalid_argument, "dots", "spacing", [&] {
            commands.dots(
                image,
                {.spacing = 0.5f, .radius = 0, .color = {0, 0, 0, 1}, .background = {0, 0, 0, 1}});
        });
        test::error(ErrorCode::invalid_argument, "dots", "radius", [&] {
            commands.dots(
                image,
                {.spacing = 4, .radius = 3, .color = {0, 0, 0, 1}, .background = {0, 0, 0, 1}});
        });
        test::error(ErrorCode::invalid_argument, "dots", "softness", [&] {
            commands.dots(image, {.spacing = 4,
                                  .radius = 1,
                                  .color = {0, 0, 0, 1},
                                  .background = {0, 0, 0, 1},
                                  .offset = {0, 0},
                                  .softness = std::numeric_limits<float>::infinity()});
        });
        commands.dots(image, {.spacing = 4,
                              .radius = 1,
                              .color = {0.75f, 0.75f, 0.75f, 0.5f},
                              .background = {1, 1, 1, 0.5f}});
        commands.brightness(image, {.amount = -1.0f});
        ctx.submit_and_wait(commands);
        const auto actual = test::read(ctx, image);
        for (int x = 0; x < 3; ++x) {
            test::near(actual[x * 4], test::channel(0.5f + 0.25f * x), 1);
            test::near(actual[x * 4 + 3], 128, 1);
        }
    });
    return test::finish();
}
