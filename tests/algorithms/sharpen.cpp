#include "../test.h"
#include <array>
#include <limits>
using namespace wgpupixel;

namespace {
std::vector<float> reference(std::span<const float> pixels, int width, int height, double strength,
                             double adjustment) {
    std::vector<float> output(pixels.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int i = (y * width + x) * 4;
            if (pixels[i + 3] == 0) {
                continue;
            }
            output[i + 3] = pixels[i + 3];
            for (int c = 0; c < 3; ++c) {
                double value = pixels[i + c];
                for (const auto offset :
                     {std::array{-1, 0}, std::array{1, 0}, std::array{0, -1}, std::array{0, 1}}) {
                    const int xx = std::clamp(x + offset[0], 0, width - 1),
                              yy = std::clamp(y + offset[1], 0, height - 1);
                    value += strength * (pixels[i + c] - pixels[(yy * width + xx) * 4 + c]);
                }
                output[i + c] = float(value + adjustment * pixels[i + 3]);
            }
        }
    }
    return output;
}
void expect(Context& ctx, const Image& image, std::span<const float> values) {
    const auto actual = test::read(ctx, image), expected = test::encode(values);
    for (std::size_t i = 0; i < actual.size(); ++i) {
        test::check(std::abs(int(actual[i]) - int(expected[i])) <= 2,
                    "sharpen CPU reference mismatch at " + std::to_string(i));
    }
}
} // namespace
int main() {
    test::run("sharpen premultiplied CPU oracle, alpha and edge extension", [] {
        auto ctx = Context::create();
        for (auto size : {std::array{5, 3}, std::array{1, 5}, std::array{5, 1}, std::array{1, 1}}) {
            const int width = size[0], height = size[1];
            auto source = ctx.create_image({width, height}),
                 dest = ctx.create_image({width, height});
            std::vector<float> pixels;
            for (int i = 0; i < width * height; ++i) {
                const float a = (i % 4) * 0.25f;
                pixels.insert(pixels.end(), {a * (i % 5) * 0.2f, a * (i % 3) * 0.3f, a * 0.2f, a});
            }
            test::paint(ctx, source, pixels);
            for (float strength : {0.f, 0.25f, 1.f}) {
                auto cmd = ctx.create_commands(2);
                cmd.sharpen(source, dest, {.strength = strength});
                cmd.brightness(dest, {.amount = 0.2f});
                ctx.submit_and_wait(cmd);
                expect(ctx, dest, reference(pixels, width, height, strength, 0.2));
            }
            std::vector<float> constant;
            for (int i = 0; i < width * height; ++i) {
                constant.insert(constant.end(), {0.1f, 0.2f, 0.3f, 0.5f});
            }
            test::paint(ctx, source, constant);
            auto cmd = ctx.create_commands(1);
            cmd.sharpen(source, dest);
            ctx.submit_and_wait(cmd);
            expect(ctx, dest, constant);
            ctx.destroy(source);
            ctx.destroy(dest);
        }
    });
    test::run("sharpen retains generated HDR and negative lobes before adjustments", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({3, 1}), dest = ctx.create_image({3, 1});
        const std::array<float, 12> pixels{0, 0, 0, 0.5f, 0.5f, 0.3f, 0.2f, 0.5f, 0, 0, 0, 0.5f};
        test::paint(ctx, source, pixels);
        for (float adjustment : {-2.f, 1.2f}) {
            auto cmd = ctx.create_commands(2);
            cmd.sharpen(source, dest, {.strength = 1});
            cmd.brightness(dest, {.amount = adjustment});
            ctx.submit_and_wait(cmd);
            expect(ctx, dest, reference(pixels, 3, 1, 1, adjustment));
        }
    });
    test::run("sharpen rejects invalid arguments without consuming slots", [] {
        auto ctx = Context::create();
        auto other = Context::create();
        auto source = ctx.create_image({2, 1}), dest = ctx.create_image({2, 1}),
             wrong = ctx.create_image({1, 2});
        auto foreign = other.create_image({2, 1});
        const std::array<float, 8> pixels{0.1f, 0.2f, 0.3f, 0.5f, 0.1f, 0.2f, 0.3f, 0.5f};
        test::paint(ctx, source, pixels);
        auto cmd = ctx.create_commands(1);
        for (float strength :
             {-1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(),
              std::numeric_limits<float>::max()}) {
            test::error(ErrorCode::invalid_argument, "sharpen", "strength",
                        [&] { cmd.sharpen(source, dest, {.strength = strength}); });
        }
        test::error(ErrorCode::invalid_argument, "sharpen", "destination",
                    [&] { cmd.sharpen(source, source); });
        test::error(ErrorCode::invalid_argument, "sharpen", "destination",
                    [&] { cmd.sharpen(source, wrong); });
        test::error(ErrorCode::invalid_resource, "sharpen", "source",
                    [&] { cmd.sharpen(foreign, dest); });
        test::error(ErrorCode::invalid_resource, "sharpen", "destination",
                    [&] { cmd.sharpen(source, foreign); });
        cmd.sharpen(source, dest, {.strength = 0});
        ctx.submit_and_wait(cmd);
        expect(ctx, dest, pixels);
    });
    return test::finish();
}
