#include "../test.h"
#include <array>
using namespace wgpupixel;

namespace {
std::vector<float> reference(std::span<const float> pixels, int width, int height,
                             float adjustment = 0) {
    constexpr int kx[9] = {-1, 0, 1, -2, 0, 2, -1, 0, 1};
    constexpr int ky[9] = {-1, -2, -1, 0, 0, 0, 1, 2, 1};
    std::vector<float> result(pixels.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int index = (y * width + x) * 4;
            const double alpha = pixels[index + 3];
            if (alpha == 0) {
                continue;
            }
            double gx = 0, gy = 0;
            for (int j = -1; j <= 1; ++j) {
                for (int i = -1; i <= 1; ++i) {
                    const int xx = std::clamp(x + i, 0, width - 1),
                              yy = std::clamp(y + j, 0, height - 1);
                    const int p = (yy * width + xx) * 4;
                    double value = 0;
                    if (pixels[p + 3] != 0) {
                        value =
                            (0.2126 * pixels[p] + 0.7152 * pixels[p + 1] + 0.0722 * pixels[p + 2]) /
                            pixels[p + 3];
                    }
                    gx += value * kx[(j + 1) * 3 + i + 1];
                    gy += value * ky[(j + 1) * 3 + i + 1];
                }
            }
            const float v = float((std::hypot(gx, gy) + adjustment) * alpha);
            for (int c = 0; c < 3; ++c) {
                result[index + c] = v;
            }
            result[index + 3] = float(alpha);
        }
    }
    return result;
}
void expect(Context& ctx, const Image& image, std::span<const float> values) {
    const auto actual = test::read(ctx, image), expected = test::encode(values);
    for (std::size_t i = 0; i < actual.size(); ++i) {
        test::check(std::abs(int(actual[i]) - int(expected[i])) <= 2,
                    "Sobel CPU reference mismatch at " + std::to_string(i));
    }
}
} // namespace

int main() {
    test::run("Sobel colored ramps, alpha, transparent neighbors and clamped borders", [] {
        auto ctx = Context::create();
        for (auto size : {std::array{5, 3}, std::array{1, 5}, std::array{5, 1}, std::array{1, 1}}) {
            const int width = size[0], height = size[1];
            auto source = ctx.create_image({width, height}),
                 dest = ctx.create_image({width, height});
            std::vector<float> pixels;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const float a = ((x + 2 * y) % 4) * 0.25f;
                    pixels.insert(pixels.end(),
                                  {a * x * 0.035f, a * y * 0.025f, a * (x + y) * 0.01f, a});
                }
            }
            test::paint(ctx, source, pixels);
            auto cmd = ctx.create_commands(1);
            cmd.sobel(source, dest);
            ctx.submit_and_wait(cmd);
            expect(ctx, dest, reference(pixels, width, height));
            ctx.destroy(source);
            ctx.destroy(dest);
        }
    });
    test::run("Sobel known linear ramp and HDR gradient survives adjustment", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({3, 1}), dest = ctx.create_image({3, 1});
        const std::array<float, 12> ramp{0,      0,    0,     0.5f,  0.125f, 0.125f,
                                         0.125f, 0.5f, 0.25f, 0.25f, 0.25f,  0.5f};
        test::paint(ctx, source, ramp);
        auto cmd = ctx.create_commands(2);
        cmd.sobel(source, dest);
        cmd.brightness(dest, {.amount = -1.5f});
        ctx.submit_and_wait(cmd);
        // Straight gradient magnitude is 1 at each edge and 2 at the center.
        expect(ctx, dest,
               std::array<float, 12>{-0.25f, -0.25f, -0.25f, 0.5f, 0.25f, 0.25f, 0.25f, 0.5f,
                                     -0.25f, -0.25f, -0.25f, 0.5f});
    });
    test::run("Sobel constant straight color is independent of nonzero alpha", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({3, 1}), dest = ctx.create_image({3, 1});
        const std::array<float, 12> pixels{0.125f, 0.25f, 0.5f, 0.25f, 0.25f, 0.5f,
                                           1,      0.5f,  0.5f, 1,     2,     1};
        test::paint(ctx, source, pixels);
        auto cmd = ctx.create_commands(1);
        cmd.sobel(source, dest);
        ctx.submit_and_wait(cmd);
        expect(ctx, dest, std::array<float, 12>{0, 0, 0, 0.25f, 0, 0, 0, 0.5f, 0, 0, 0, 1});
    });
    test::run("Sobel finite HDR constant cancels and small alpha retains straight edges", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({3, 1}), dest = ctx.create_image({3, 1});
        auto cmd = ctx.create_commands(2);
        cmd.fill(source, {.color = {1e30f, -2e30f, 3e30f, 1}});
        cmd.sobel(source, dest);
        ctx.submit_and_wait(cmd);
        expect(ctx, dest, std::array<float, 12>{0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1});
        // Power-of-two alpha avoids fixture division noise. Export retains RGB
        // even though the quantized alpha byte is zero.
        const float a = std::ldexp(1.f, -60);
        const std::array<float, 12> pixels{0,      0, 0,      a,     a / 32, a / 16,
                                           a / 64, a, a / 16, a / 8, a / 32, a};
        test::paint(ctx, source, pixels);
        cmd.sobel(source, dest);
        ctx.submit_and_wait(cmd);
        expect(ctx, dest, reference(pixels, 3, 1));
    });
    test::run("Sobel alias dimensions and context errors preserve slots", [] {
        auto ctx = Context::create();
        auto other = Context::create();
        auto source = ctx.create_image({2, 1}), dest = ctx.create_image({2, 1}),
             wrong = ctx.create_image({1, 2});
        auto foreign = other.create_image({2, 1});
        test::paint(ctx, source,
                    std::array<float, 8>{0.25f, 0.25f, 0.25f, 0.5f, 0.25f, 0.25f, 0.25f, 0.5f});
        auto cmd = ctx.create_commands(1);
        test::error(ErrorCode::invalid_argument, "sobel", "destination",
                    [&] { cmd.sobel(source, source); });
        test::error(ErrorCode::invalid_argument, "sobel", "destination",
                    [&] { cmd.sobel(source, wrong); });
        test::error(ErrorCode::invalid_resource, "sobel", "source",
                    [&] { cmd.sobel(foreign, dest); });
        test::error(ErrorCode::invalid_resource, "sobel", "destination",
                    [&] { cmd.sobel(source, foreign); });
        cmd.sobel(source, dest);
        ctx.submit_and_wait(cmd);
        expect(ctx, dest, std::array<float, 8>{0, 0, 0, 0.5f, 0, 0, 0, 0.5f});
    });
    return test::finish();
}
