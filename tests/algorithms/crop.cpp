#include "../test.h"
#include <array>
#include <limits>

using namespace wgpupixel;

int main() {
    test::run("crop copies rectangles and pads outside including extreme origins", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({5, 3});
        std::vector<float> pixels;
        for (int i = 0; i < 15; ++i) {
            const float a = (i % 4) * 0.25f;
            pixels.insert(pixels.end(), {a * (i % 5) / 4, a * (i % 3) / 2, a * 0.3f, a});
        }
        test::paint(ctx, source, pixels);
        const auto original = test::read(ctx, source);
        for (auto size : {std::array{3, 2}, std::array{7, 5}, std::array{1, 1}}) {
            auto destination = ctx.create_image({size[0], size[1]});
            for (auto origin :
                 {Position{1, 1}, Position{0, 0}, Position{-2, -1}, Position{4, 2}, Position{5, 0},
                  Position{0, 3}, Position{std::numeric_limits<std::int32_t>::min(), 0},
                  Position{0, std::numeric_limits<std::int32_t>::max()},
                  Position{std::numeric_limits<std::int32_t>::max(),
                           std::numeric_limits<std::int32_t>::min()}}) {
                auto cmd = ctx.create_commands(2);
                cmd.fill(destination, {.color = {1, 1, 1, 1}});
                cmd.crop(source, destination, {.origin = origin});
                ctx.submit_and_wait(cmd);
                const auto actual = test::read(ctx, destination);
                for (int y = 0; y < size[1]; ++y) {
                    for (int x = 0; x < size[0]; ++x) {
                        const auto sx = std::int64_t(origin.x) + x;
                        const auto sy = std::int64_t(origin.y) + y;
                        for (int c = 0; c < 4; ++c) {
                            const auto expected = sx >= 0 && sy >= 0 && sx < 5 && sy < 3
                                                      ? original[std::size_t(sy * 5 + sx) * 4 + c]
                                                      : 0;
                            test::check(actual[(y * size[0] + x) * 4 + c] == expected,
                                        "crop rectangle or transparent padding differs");
                        }
                    }
                }
            }
            ctx.destroy(destination);
        }
    });
    test::run("crop retains HDR and validates aliases and contexts atomically", [] {
        auto ctx = Context::create();
        auto other = Context::create();
        auto source = ctx.create_image({2, 1});
        auto destination = ctx.create_image({1, 1});
        auto foreign = other.create_image({1, 1});
        test::paint(ctx, source,
                    std::array<float, 8>{-0.25f, -0.1f, 0, 0.5f, 0.75f, 1, 0.9f, 0.5f});
        auto cmd = ctx.create_commands(2);
        test::error(ErrorCode::invalid_argument, "crop", "destination",
                    [&] { cmd.crop(source, source, {.origin = {0, 0}}); });
        test::error(ErrorCode::invalid_resource, "crop", "source",
                    [&] { cmd.crop(foreign, destination, {.origin = {0, 0}}); });
        test::error(ErrorCode::invalid_resource, "crop", "destination",
                    [&] { cmd.crop(source, foreign, {.origin = {0, 0}}); });
        cmd.crop(source, destination, {.origin = {1, 0}});
        cmd.brightness(destination, {.amount = -1});
        ctx.submit_and_wait(cmd);
        auto actual = test::read(ctx, destination);
        test::near(actual[0], test::channel(0.5f), 1);
        test::near(actual[1], test::channel(1), 1);
        test::near(actual[2], test::channel(0.8f), 1);
        test::near(actual[3], 128, 1);
        auto negative = ctx.create_commands(2);
        negative.crop(source, destination, {.origin = {0, 0}});
        negative.brightness(destination, {.amount = 0.6f});
        ctx.submit_and_wait(negative);
        actual = test::read(ctx, destination);
        test::near(actual[0], test::channel(0.1f), 1);
        test::near(actual[1], test::channel(0.4f), 1);
    });
    return test::finish();
}
