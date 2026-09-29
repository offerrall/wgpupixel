#include "../test.h"
#include <array>
#include <limits>

using namespace wgpupixel;

int main() {
    test::run("grid periodic boundaries, negative and extreme offsets", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({7, 5});
        for (std::int64_t spacing : {std::int64_t{1}, std::int64_t{3},
                                     std::int64_t{std::numeric_limits<std::int32_t>::max()}}) {
            for (std::int64_t width : {std::int64_t{1}, spacing}) {
                for (Position offset : {Position{0, 0}, Position{-3, -6}, Position{-4, 2},
                                        Position{std::numeric_limits<std::int32_t>::min(),
                                                 std::numeric_limits<std::int32_t>::max()}}) {
                    auto commands = ctx.create_commands(1);
                    commands.grid(image, {.spacing = spacing,
                                          .line_width = width,
                                          .color = {0.125f, 0, 0, 0.25f},
                                          .background = {0, 0.25f, 0.5f, 0.5f},
                                          .offset = offset});
                    ctx.submit_and_wait(commands);
                    std::vector<float> expected;
                    const auto modulo = [spacing](std::int64_t n) {
                        return (n % spacing + spacing) % spacing;
                    };
                    for (int y = 0; y < 5; ++y) {
                        for (int x = 0; x < 7; ++x) {
                            const bool line = modulo(std::int64_t{x} + offset.x) < width ||
                                              modulo(std::int64_t{y} + offset.y) < width;
                            const std::array<float, 4> color =
                                line ? std::array{0.125f, 0.0f, 0.0f, 0.25f}
                                     : std::array{0.0f, 0.25f, 0.5f, 0.5f};
                            expected.insert(expected.end(), color.begin(), color.end());
                        }
                    }
                    const auto encoded = test::encode(expected);
                    const auto actual = test::read(ctx, image);
                    for (std::size_t i = 0; i < encoded.size(); ++i) {
                        test::check(std::abs(int(actual[i]) - int(encoded[i])) <= 1,
                                    "grid differs from integer CPU reference");
                    }
                }
            }
        }
    });
    test::run("grid HDR and validation atomicity", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 1});
        auto commands = ctx.create_commands(2);
        for (std::int64_t invalid : {std::int64_t{0}, std::int64_t{-1},
                                     std::int64_t{std::numeric_limits<std::int32_t>::max()} + 1}) {
            test::error(ErrorCode::invalid_argument, "grid", "spacing", [&] {
                commands.grid(image, {.spacing = invalid,
                                      .line_width = 1,
                                      .color = {0, 0, 0, 1},
                                      .background = {1, 1, 1, 1}});
            });
        }
        for (int invalid : {0, -1, 4}) {
            test::error(ErrorCode::invalid_argument, "grid", "line_width", [&] {
                commands.grid(image, {.spacing = 3,
                                      .line_width = invalid,
                                      .color = {0, 0, 0, 1},
                                      .background = {1, 1, 1, 1}});
            });
        }
        test::error(ErrorCode::invalid_argument, "grid", "background", [&] {
            commands.grid(
                image,
                {.spacing = 3, .line_width = 1, .color = {0, 0, 0, 1}, .background = {0, 0, 0, 2}});
        });
        commands.grid(image, {.spacing = 2,
                              .line_width = 1,
                              .color = {0.75f, 0.75f, 0.75f, 0.5f},
                              .background = {1, 1, 1, 0.5f},
                              .offset = {0, 1}});
        commands.brightness(image, {.amount = -1.0f});
        ctx.submit_and_wait(commands);
        const auto actual = test::read(ctx, image);
        for (int x = 0; x < 3; ++x) {
            test::near(actual[x * 4], test::channel(x % 2 == 0 ? 0.5f : 1.0f), 1);
            test::near(actual[x * 4 + 3], 128, 1);
        }
    });
    return test::finish();
}
