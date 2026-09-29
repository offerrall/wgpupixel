#include "../test.h"
#include <array>
#include <limits>
using namespace wgpupixel;
namespace {
std::vector<float> reference(std::span<const float> pixels, int width, int height, double strength,
                             double adjustment = 0) {
    constexpr int kernel[9] = {-2, -1, 0, -1, 1, 1, 0, 1, 2};
    std::vector<float> result(pixels.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int index = (y * width + x) * 4;
            const double alpha = pixels[index + 3];
            if (alpha == 0) {
                continue;
            }
            for (int c = 0; c < 3; ++c) {
                double sum = 0;
                for (int j = -1; j <= 1; ++j) {
                    for (int i = -1; i <= 1; ++i) {
                        const int xx = std::clamp(x + i, 0, width - 1),
                                  yy = std::clamp(y + j, 0, height - 1);
                        const int p = (yy * width + xx) * 4;
                        if (pixels[p + 3] != 0) {
                            sum +=
                                kernel[(j + 1) * 3 + i + 1] * double(pixels[p + c]) / pixels[p + 3];
                        }
                    }
                }
                result[index + c] = float((sum * strength + 0.5 + adjustment) * alpha);
            }
            result[index + 3] = float(alpha);
        }
    }
    return result;
}
void expect(Context& ctx, const Image& image, std::span<const float> pixels) {
    const auto actual = test::read(ctx, image), expected = test::encode(pixels);
    for (std::size_t i = 0; i < actual.size(); ++i) {
        test::check(std::abs(int(actual[i]) - int(expected[i])) <= 2,
                    "emboss CPU reference mismatch at " + std::to_string(i));
    }
}
} // namespace
int main() {
    test::run("emboss straight RGB matrix, semi-alpha and boundary extension", [] {
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
            for (float strength : {0.f, 0.25f, 1.f}) {
                auto cmd = ctx.create_commands(1);
                cmd.emboss(source, dest, {.strength = strength});
                ctx.submit_and_wait(cmd);
                expect(ctx, dest, reference(pixels, width, height, strength));
            }
            ctx.destroy(source);
            ctx.destroy(dest);
        }
    });
    test::run("emboss retains generated HDR and negative RGB", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({3, 1}), dest = ctx.create_image({3, 1});
        const std::array<float, 12> pixels{0,    0.5f, 0,    0.5f, 0.25f, 0.25f,
                                           0.1f, 0.5f, 0.5f, 0,    0.2f,  0.5f};
        test::paint(ctx, source, pixels);
        for (float adjustment : {-2.f, 1.5f}) {
            auto cmd = ctx.create_commands(2);
            cmd.emboss(source, dest, {.strength = 1});
            cmd.brightness(dest, {.amount = adjustment});
            ctx.submit_and_wait(cmd);
            expect(ctx, dest, reference(pixels, 3, 1, 1, adjustment));
        }
    });
    test::run("emboss constant straight color and zero strength preserve alpha", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({3, 1}), dest = ctx.create_image({3, 1});
        const std::array<float, 12> pixels{0.025f, 0.05f, 0.075f, 0.25f, 0.05f, 0.1f,
                                           0.15f,  0.5f,  0.1f,   0.2f,  0.3f,  1};
        test::paint(ctx, source, pixels);
        auto cmd = ctx.create_commands(1);
        cmd.emboss(source, dest, {.strength = 1});
        ctx.submit_and_wait(cmd);
        expect(ctx, dest, reference(pixels, 3, 1, 1));
    });
    test::run("emboss HDR constant cancellation and tiny alpha zero strength", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({3, 1}), dest = ctx.create_image({3, 1});
        auto cmd = ctx.create_commands(2);
        cmd.fill(source, {.color = {1e30f, -1e30f, 2e30f, 1}});
        cmd.emboss(source, dest, {.strength = 1e-31f});
        ctx.submit_and_wait(cmd);
        std::vector<float> pixels;
        for (int x = 0; x < 3; ++x) {
            pixels.insert(pixels.end(), {1e30f, -1e30f, 2e30f, 1});
        }
        expect(ctx, dest, reference(pixels, 3, 1, double(1e-31f)));
        // Zero strength must bypass unpremultiplication, even if straight RGB
        // would overflow float32. The nonzero alpha itself remains normal.
        const float a = std::ldexp(1.f, -60);
        cmd.fill(source, {.color = {1e30f, -1e30f, 2e30f, a}});
        cmd.emboss(source, dest, {.strength = 0});
        ctx.submit_and_wait(cmd);
        pixels.clear();
        for (int x = 0; x < 3; ++x) {
            pixels.insert(pixels.end(), {.5f * a, .5f * a, .5f * a, a});
        }
        expect(ctx, dest, pixels);
    });
    test::run("emboss validates strength dimensions alias and context atomically", [] {
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
            test::error(ErrorCode::invalid_argument, "emboss", "strength",
                        [&] { cmd.emboss(source, dest, {.strength = strength}); });
        }
        test::error(ErrorCode::invalid_argument, "emboss", "destination",
                    [&] { cmd.emboss(source, source); });
        test::error(ErrorCode::invalid_argument, "emboss", "destination",
                    [&] { cmd.emboss(source, wrong); });
        test::error(ErrorCode::invalid_resource, "emboss", "source",
                    [&] { cmd.emboss(foreign, dest); });
        test::error(ErrorCode::invalid_resource, "emboss", "destination",
                    [&] { cmd.emboss(source, foreign); });
        cmd.emboss(source, dest, {.strength = 0});
        ctx.submit_and_wait(cmd);
        expect(ctx, dest,
               std::array<float, 8>{0.25f, 0.25f, 0.25f, 0.5f, 0.25f, 0.25f, 0.25f, 0.5f});
    });
    return test::finish();
}
