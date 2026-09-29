#include "../test.h"
#include <limits>

using namespace wgpupixel;

int main() {
    test::run("stripes orientation, periodicity, alpha and edge coverage", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({7, 5});
        for (float angle : {0.0f, 90.0f, 360.0f}) {
            for (float offset : {-4.0f, 0.0f, 4.0f}) {
                for (float width : {0.0f, 2.0f, 4.0f}) {
                    auto commands = ctx.create_commands(1);
                    commands.stripes(image, {.angle = angle,
                                             .spacing = 4,
                                             .width = width,
                                             .first = {0.125f, 0, 0, 0.25f},
                                             .second = {0, 0.25f, 0.5f, 0.5f},
                                             .offset = offset});
                    ctx.submit_and_wait(commands);
                    std::vector<float> expected;
                    for (int y = 0; y < 5; ++y) {
                        for (int x = 0; x < 7; ++x) {
                            const int phase = (angle == 90 ? y : x) % 4;
                            float coverage = phase == 0 || phase == 2 ? 0.5f
                                             : phase == 1             ? 1.0f
                                                                      : 0.0f;
                            if (width == 0) {
                                coverage = 0;
                            }
                            if (width == 4) {
                                coverage = 1;
                            }
                            expected.insert(expected.end(),
                                            {0.125f * coverage, 0.25f * (1 - coverage),
                                             0.5f * (1 - coverage), 0.5f - 0.25f * coverage});
                        }
                    }
                    const auto encoded = test::encode(expected);
                    const auto actual = test::read(ctx, image);
                    for (std::size_t i = 0; i < encoded.size(); ++i) {
                        test::check(std::abs(int(actual[i]) - int(encoded[i])) <= 1,
                                    "stripes differ from known coverage");
                    }
                }
            }
        }
    });
    test::run("stripes large spacing preserves negative boundary distances", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 1});
        auto commands = ctx.create_commands(1);
        commands.stripes(image, {.angle = 180,
                                 .spacing = 1e20f,
                                 .width = 2,
                                 .first = {1, 1, 1, 1},
                                 .second = {0, 0, 0, 1}});
        ctx.submit_and_wait(commands);
        const auto actual = test::read(ctx, image);
        test::near(actual[0], test::channel(0.5f), 1);
        test::near(actual[4], 0, 1);
        test::near(actual[8], 0, 1);
    });
    test::run("stripes HDR and parameter atomicity", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 1});
        auto commands = ctx.create_commands(2);
        test::error(ErrorCode::invalid_argument, "stripes", "angle", [&] {
            commands.stripes(image, {.angle = std::numeric_limits<float>::infinity(),
                                     .spacing = 4,
                                     .width = 2,
                                     .first = {0, 0, 0, 1},
                                     .second = {0, 0, 0, 1}});
        });
        test::error(ErrorCode::invalid_argument, "stripes", "spacing", [&] {
            commands.stripes(image, {.angle = 0,
                                     .spacing = 0,
                                     .width = 0,
                                     .first = {0, 0, 0, 1},
                                     .second = {0, 0, 0, 1}});
        });
        test::error(ErrorCode::invalid_argument, "stripes", "width", [&] {
            commands.stripes(image, {.angle = 0,
                                     .spacing = 2,
                                     .width = 3,
                                     .first = {0, 0, 0, 1},
                                     .second = {0, 0, 0, 1}});
        });
        commands.stripes(image, {.angle = 0,
                                 .spacing = 4,
                                 .width = 2,
                                 .first = {0.75f, 0.75f, 0.75f, 0.5f},
                                 .second = {1, 1, 1, 0.5f}});
        commands.brightness(image, {.amount = -1.0f});
        ctx.submit_and_wait(commands);
        const auto actual = test::read(ctx, image);
        for (int x = 0; x < 3; ++x) {
            test::near(actual[x * 4], test::channel(x == 1 ? 0.5f : 0.75f), 1);
            test::near(actual[x * 4 + 3], 128, 1);
        }
    });
    return test::finish();
}
