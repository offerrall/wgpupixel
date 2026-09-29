#include "../test.h"
#include <array>

using namespace wgpupixel;

int main() {
    test::run("flip reverses exact non-square pixels and degenerate axes", [] {
        auto ctx = Context::create();
        for (auto size : {std::array{5, 3}, std::array{1, 5}, std::array{5, 1}, std::array{1, 1}}) {
            const int width = size[0], height = size[1];
            auto source = ctx.create_image({width, height});
            auto destination = ctx.create_image({width, height});
            std::vector<float> pixels;
            for (int i = 0; i < width * height; ++i) {
                const float a = (i % 4) * 0.25f;
                pixels.insert(pixels.end(), {a * (i % 5) / 4, a * (i % 3) / 2, a * 0.3f, a});
            }
            test::paint(ctx, source, pixels);
            const auto original = test::read(ctx, source);
            for (auto direction :
                 {FlipDirection::horizontal, FlipDirection::vertical, FlipDirection::both}) {
                auto cmd = ctx.create_commands(1);
                cmd.flip(source, destination, {.direction = direction});
                ctx.submit_and_wait(cmd);
                const auto actual = test::read(ctx, destination);
                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        const int sx = direction == FlipDirection::vertical ? x : width - x - 1;
                        const int sy = direction == FlipDirection::horizontal ? y : height - y - 1;
                        for (int c = 0; c < 4; ++c) {
                            test::check(actual[(y * width + x) * 4 + c] ==
                                            original[(sy * width + sx) * 4 + c],
                                        "flip did not preserve exact encoded RGBA");
                        }
                    }
                }
            }
            ctx.destroy(source);
            ctx.destroy(destination);
        }
    });
    test::run("flip preserves HDR, negative color and rejects errors atomically", [] {
        auto ctx = Context::create();
        auto other = Context::create();
        auto source = ctx.create_image({2, 1});
        auto destination = ctx.create_image({2, 1});
        auto wrong_size = ctx.create_image({1, 2});
        auto foreign = other.create_image({2, 1});
        const std::array<float, 8> pixels{0.75f, 1, 0.9f, 0.5f, -0.1f, -0.25f, 0, 0.5f};
        test::paint(ctx, source, pixels);
        auto cmd = ctx.create_commands(2);
        test::error(ErrorCode::invalid_argument, "flip", "direction", [&] {
            cmd.flip(source, destination, {.direction = static_cast<FlipDirection>(99)});
        });
        test::error(ErrorCode::invalid_argument, "flip", "destination",
                    [&] { cmd.flip(source, source); });
        test::error(ErrorCode::invalid_argument, "flip", "destination",
                    [&] { cmd.flip(source, wrong_size); });
        test::error(ErrorCode::invalid_resource, "flip", "source",
                    [&] { cmd.flip(foreign, destination); });
        test::error(ErrorCode::invalid_resource, "flip", "destination",
                    [&] { cmd.flip(source, foreign); });
        cmd.flip(source, destination);
        cmd.brightness(destination, {.amount = -1});
        ctx.submit_and_wait(cmd);
        const auto actual = test::read(ctx, destination);
        const auto expected =
            test::encode(std::array<float, 8>{-0.6f, -0.75f, -0.5f, 0.5f, 0.25f, 0.5f, 0.4f, 0.5f});
        for (std::size_t i = 0; i < actual.size(); ++i) {
            test::check(std::abs(int(actual[i]) - int(expected[i])) <= 1,
                        "flip clipped HDR before adjustment");
        }
        auto negative = ctx.create_commands(2);
        negative.flip(source, destination);
        negative.brightness(destination, {.amount = 0.6f});
        ctx.submit_and_wait(negative);
        const auto adjusted = test::read(ctx, destination);
        test::near(adjusted[0], test::channel(0.4f), 1);
        test::near(adjusted[1], test::channel(0.1f), 1);
    });
    return test::finish();
}
