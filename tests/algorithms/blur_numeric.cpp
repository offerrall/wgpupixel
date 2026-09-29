#include "../test.h"
#include <array>
#include <limits>
using namespace wgpupixel;

namespace {
void expect_constant(Context& ctx, const Image& image, float linear) {
    const auto output = test::read(ctx, image);
    for (std::size_t i = 0; i < output.size(); i += 4) {
        for (int c = 0; c < 3; ++c) {
            test::near(output[i + c], test::channel(linear), 2);
        }
        test::near(output[i + 3], 255, 0);
    }
}
} // namespace

int main() {
    test::run(
        "blur stops zero support at extreme admitted radius with masks and edge extension", [] {
            auto ctx = Context::create();
            auto image = ctx.create_image({5, 3}), scratch = ctx.create_image({5, 3});
            auto mask = ctx.create_mask({5, 3});
            auto upload = ctx.create_upload_buffer(mask);
            const std::array<std::uint8_t, 15> coverage{0,   128, 255, 64,  255, 255, 0,  128,
                                                        255, 64,  64,  128, 0,   255, 255};
            ctx.write(upload, coverage);
            auto commands = ctx.create_commands(4);
            commands.upload(upload, mask);
            ctx.submit_and_wait(commands);
            std::vector<float> pixels;
            for (int i = 0; i < 15; ++i) {
                pixels.insert(pixels.end(), {float(i) / 20, float(14 - i) / 20, .25f, 1});
            }
            for (const auto* supplied :
                 {static_cast<const Mask*>(nullptr), static_cast<const Mask*>(&mask)}) {
                for (const float sigma : {.1f, 1e-20f}) {
                    test::paint(ctx, image, pixels);
                    commands.gaussian_blur(
                        image,
                        test::reserve_workspace(
                            ctx, image,
                            GaussianBlurOptions{.radius = std::numeric_limits<std::int32_t>::max(),
                                                .sigma = sigma,
                                                .mask = supplied},
                            gaussian_blur_requirements));
                    ctx.submit_and_wait(commands);
                    const auto actual = test::read(ctx, image), expected = test::encode(pixels);
                    for (std::size_t i = 0; i < actual.size(); ++i) {
                        test::near(actual[i], expected[i], 1);
                    }
                }
            }
        });
    test::run("blur preserves positive and negative HDR constants before finite export", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 3}), scratch = ctx.create_image({3, 3}),
             canvas = ctx.create_image({3, 3});
        auto commands = ctx.create_commands(8);
        for (float sign : {-1.f, 1.f}) {
            commands.fill(image, {.color = {sign * 1e38f, sign * 1e38f, sign * 1e38f, 1}});
            commands.fill(canvas, {.color = {0, 0, 0, 1}});
            commands.gaussian_blur(
                image,
                test::reserve_workspace(ctx, image, GaussianBlurOptions{.radius = 3, .sigma = 1.5f},
                                        gaussian_blur_requirements));
            commands.opacity(image, {.factor = 1e-37f});
            commands.blend(image, canvas, {.position = {0, 0}});
            commands.brightness(canvas, {.amount = .5f - sign * 10.f});
            ctx.submit_and_wait(commands);
            expect_constant(ctx, canvas, .5f);
        }
    });
    test::run("blur independently scales HDR channels without losing small alpha", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 3}), scratch = ctx.create_image({3, 3});
        auto commands = ctx.create_commands(3);
        commands.fill(image, {.color = {1e38f, 1e-20f, 1e-20f, 1e-20f}});
        commands.gaussian_blur(
            image,
            test::reserve_workspace(ctx, image, GaussianBlurOptions{.radius = 3, .sigma = 1.5f},
                                    gaussian_blur_requirements));
        // Export green/blue tests their ratio to the independently preserved alpha.
        ctx.submit_and_wait(commands);
        const auto pixels = test::read(ctx, image);
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            test::near(pixels[i + 1], 255, 1);
            test::near(pixels[i + 2], 255, 1);
        }
    });
    test::run("blur mixed signed HDR neighbors match a float64 convolution", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 1}), scratch = ctx.create_image({3, 1}),
             canvas = ctx.create_image({3, 1});
        const std::array<float, 3> values{1e38f, -1e38f, 5e37f};
        std::vector<float> pixels;
        for (float value : values) {
            pixels.insert(pixels.end(), {value, value, value, 1});
        }
        test::paint(ctx, image, pixels);
        auto commands = ctx.create_commands(7);
        commands.fill(canvas, {.color = {0, 0, 0, 1}});
        commands.gaussian_blur(
            image,
            test::reserve_workspace(ctx, image, GaussianBlurOptions{.radius = 3, .sigma = 1.5f},
                                    gaussian_blur_requirements));
        commands.opacity(image, {.factor = 1e-37f});
        commands.blend(image, canvas, {.position = {0, 0}});
        commands.brightness(canvas, {.amount = -2.3f});
        ctx.submit_and_wait(commands);
        const auto actual = test::read(ctx, canvas);
        for (int x = 0; x < 3; ++x) {
            double sum = 0, normalization = 0;
            for (int offset = -3; offset <= 3; ++offset) {
                const double weight = std::exp(-double(offset * offset) / (2 * 1.5 * 1.5));
                sum += double(values[std::clamp(x + offset, 0, 2)]) * weight;
                normalization += weight;
            }
            const float expected = float(sum / normalization * double(1e-37f) - 2.3);
            for (int c = 0; c < 3; ++c) {
                test::near(actual[x * 4 + c], test::channel(expected), 2);
            }
        }
    });
    return test::finish();
}
