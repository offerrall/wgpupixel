#include "test.h"

#include <algorithm>
#include <array>
#include <limits>

using namespace wgpupixel;

namespace {
using Pixel = std::array<float, 4>;

void expect(std::span<const std::uint8_t> actual, std::span<const float> linear,
            int tolerance = 1) {
    const auto expected = test::encode(linear);
    test::check(actual.size() == expected.size(), "pixel counts differ");
    for (std::size_t i = 0; i < actual.size(); ++i) {
        test::check(std::abs(int(actual[i]) - int(expected[i])) <= tolerance,
                    "component " + std::to_string(i) + ": expected " + std::to_string(expected[i]) +
                        ", got " + std::to_string(actual[i]));
    }
}

std::vector<float> pattern(int width, int height) {
    std::vector<float> pixels;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float alpha = static_cast<float>((x + y) % 4) / 3.0f;
            pixels.insert(pixels.end(), {static_cast<float>(x % 7) * alpha / 6.0f,
                                         static_cast<float>(y % 5) * alpha / 4.0f,
                                         static_cast<float>((x * y) % 7) * alpha / 6.0f, alpha});
        }
    }
    return pixels;
}

// Evaluate the full two-dimensional convolution in double precision. This
// reference does not share the GPU's separable implementation or summation order.
std::vector<float> blurred(std::span<const float> pixels, int width, int height, int radius,
                           double sigma) {
    std::vector<float> result(pixels.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::array<double, 4> sum{};
            double total = 0.0;
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    const double weight =
                        std::exp(-static_cast<double>(dx * dx + dy * dy) / (2.0 * sigma * sigma));
                    const int source_x = std::clamp(x + dx, 0, width - 1);
                    const int source_y = std::clamp(y + dy, 0, height - 1);
                    const auto index = static_cast<std::size_t>(source_y * width + source_x) * 4;
                    for (std::size_t channel = 0; channel < 4; ++channel) {
                        sum[channel] += pixels[index + channel] * weight;
                    }
                    total += weight;
                }
            }
            const auto index = static_cast<std::size_t>(y * width + x) * 4;
            for (std::size_t channel = 0; channel < 4; ++channel) {
                result[index + channel] = static_cast<float>(sum[channel] / total);
            }
        }
    }
    return result;
}

Pixel composited(Pixel foreground, Pixel backdrop, float opacity, BlendMode mode) {
    const double source_alpha = static_cast<double>(foreground[3]) * opacity;
    const double backdrop_alpha = backdrop[3];
    Pixel result{};
    result[3] = static_cast<float>(source_alpha + backdrop_alpha * (1.0 - source_alpha));
    for (std::size_t channel = 0; channel < 3; ++channel) {
        const double source = foreground[3] == 0 ? 0 : foreground[channel] / foreground[3];
        const double back = backdrop_alpha == 0 ? 0 : backdrop[channel] / backdrop_alpha;
        double blend = source;
        switch (mode) {
        case BlendMode::normal:
            break;
        case BlendMode::multiply:
            blend = back * source;
            break;
        case BlendMode::screen:
            blend = 1.0 - (1.0 - back) * (1.0 - source);
            break;
        case BlendMode::overlay:
            blend = back <= 0.5 ? 2.0 * back * source : 1.0 - 2.0 * (1.0 - back) * (1.0 - source);
            break;
        case BlendMode::darken:
            blend = std::min(back, source);
            break;
        case BlendMode::lighten:
            blend = std::max(back, source);
            break;
        case BlendMode::difference:
            blend = std::abs(back - source);
            break;
        case BlendMode::exclusion:
            blend = back * (1.0 - source) + source * (1.0 - back);
            break;
        }
        // Disjoint source-only, backdrop-only and overlapping coverage.
        result[channel] = static_cast<float>(source_alpha * (1.0 - backdrop_alpha) * source +
                                             backdrop_alpha * (1.0 - source_alpha) * back +
                                             source_alpha * backdrop_alpha * blend);
    }
    return result;
}
} // namespace

int main() {
    Context ctx;
    test::run("GPU context", [&] { ctx = Context::create(); });
    if (test::failures != 0) {
        return test::finish();
    }

    test::run("copy and export preserve pixels at odd dimensions", [&] {
        const auto pixels = pattern(13, 9);
        auto source = ctx.create_image({13, 9});
        auto destination = ctx.create_image({13, 9});
        test::paint(ctx, source, pixels);
        expect(test::read(ctx, source), pixels, 0);
        auto cmd = ctx.create_commands(2);
        cmd.fill(destination, {.color = {9, 8, 7, 1}});
        cmd.copy(source, destination);
        ctx.submit_and_wait(cmd);
        expect(test::read(ctx, destination), pixels, 0);
        expect(test::read(ctx, source), pixels, 0);
    });

    test::run("fill overwrites the entire logical image without clamping", [&] {
        auto image = ctx.create_image({13, 9});
        auto cmd = ctx.create_commands(1);
        for (const Pixel color : {Pixel{-2, 3, 0.25f, 0.5f}, Pixel{0, 0, 0, 0}}) {
            cmd.fill(image, {.color = {color[0], color[1], color[2], color[3]}});
            ctx.submit_and_wait(cmd);
            const auto pixels = test::read(ctx, image);
            for (std::size_t i = 0; i < pixels.size(); ++i) {
                test::near(pixels[i], test::encode(color)[i % 4], 1);
            }
        }
    });

    test::run("grayscale and brightness preserve premultiplied alpha and HDR", [&] {
        auto image = ctx.create_image({9, 5});
        auto expected = pattern(9, 5);
        test::paint(ctx, image, expected);
        auto cmd = ctx.create_commands(1);
        cmd.grayscale(image);
        ctx.submit_and_wait(cmd);
        for (std::size_t i = 0; i < expected.size(); i += 4) {
            const float gray =
                expected[i] * 0.2126f + expected[i + 1] * 0.7152f + expected[i + 2] * 0.0722f;
            expected[i] = expected[i + 1] = expected[i + 2] = gray;
        }
        expect(test::read(ctx, image), expected);
        for (const float amount : {2.0f, -4.0f, 2.0f}) {
            cmd.brightness(image, {.amount = amount});
            ctx.submit_and_wait(cmd);
            for (std::size_t i = 0; i < expected.size(); i += 4) {
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    expected[i + channel] += amount * expected[i + 3];
                }
            }
            expect(test::read(ctx, image), expected);
        }
    });

    test::run("all blend modes handle alpha, opacity and transparent pixels", [&] {
        auto source = ctx.create_image({1, 1});
        auto destination = ctx.create_image({1, 1});
        auto cmd = ctx.create_commands(1);
        const std::array modes{BlendMode::normal,     BlendMode::multiply, BlendMode::screen,
                               BlendMode::overlay,    BlendMode::darken,   BlendMode::lighten,
                               BlendMode::difference, BlendMode::exclusion};
        for (const auto mode : modes) {
            for (const Pixel foreground : {Pixel{0.1f, 0.35f, 0.6f, 0.5f}, Pixel{0, 0, 0, 0}}) {
                for (const Pixel backdrop : {Pixel{0.6f, 0.15f, -0.1f, 0.75f}, Pixel{0, 0, 0, 0}}) {
                    for (const float opacity : {0.0f, 0.4f, 1.0f}) {
                        test::paint(ctx, source, foreground);
                        test::paint(ctx, destination, backdrop);
                        cmd.blend(source, destination,
                                  {.position = {0, 0}, .opacity = opacity, .mode = mode});
                        ctx.submit_and_wait(cmd);
                        expect(test::read(ctx, destination),
                               composited(foreground, backdrop, opacity, mode));
                    }
                }
            }
        }
    });

    test::run("blend clips offsets, including signed integer limits", [&] {
        auto source = ctx.create_image({3, 2});
        auto destination = ctx.create_image({9, 5});
        const std::array<float, 24> pixels{0.125f, 0, 0, 1, 0.25f,  0, 0, 1, 0.375f, 0, 0, 1,
                                           0.5f,   0, 0, 1, 0.625f, 0, 0, 1, 0.75f,  0, 0, 1};
        test::paint(ctx, source, pixels);
        auto cmd = ctx.create_commands(2);
        const auto minimum = std::numeric_limits<std::int32_t>::min();
        const auto maximum = std::numeric_limits<std::int32_t>::max();
        for (const Position position :
             {Position{-1, -1}, Position{8, 4}, Position{minimum, 0}, Position{maximum, 0},
              Position{0, minimum}, Position{0, maximum}}) {
            cmd.fill(destination, {.color = {0, 0, 0, 0}});
            cmd.blend(source, destination, {.position = position});
            ctx.submit_and_wait(cmd);
            std::vector<float> expected(9 * 5 * 4, 0);
            for (int y = 0; y < 2; ++y) {
                for (int x = 0; x < 3; ++x) {
                    const std::int64_t dx = static_cast<std::int64_t>(x) + position.x;
                    const std::int64_t dy = static_cast<std::int64_t>(y) + position.y;
                    if (dx >= 0 && dx < 9 && dy >= 0 && dy < 5) {
                        const auto index = static_cast<std::size_t>(dy * 9 + dx) * 4;
                        expected[index] = pixels[static_cast<std::size_t>(y * 3 + x) * 4];
                        expected[index + 3] = 1;
                    }
                }
            }
            expect(test::read(ctx, destination), expected, 0);
        }
    });

    test::run("resize samples pixel centers and clamps image edges", [&] {
        auto source = ctx.create_image({2, 2});
        auto destination = ctx.create_image({4, 4});
        const std::array<float, 16> pixels{0, 0,    0, 0,    0.2f, 0,    0,    0.25f,
                                           0, 0.4f, 0, 0.5f, 0.2f, 0.4f, 0.8f, 1};
        test::paint(ctx, source, pixels);
        auto cmd = ctx.create_commands(2);
        for (const auto filter : {ResizeFilter::nearest, ResizeFilter::bilinear}) {
            test::garbage(ctx, destination, {-99, -99, -99, -99});
            cmd.resize(source, destination, {.filter = filter});
            ctx.submit_and_wait(cmd);
            std::vector<float> expected;
            // Avoid equidistant nearest-neighbor ties: their rounding direction
            // is not part of the API contract.
            constexpr std::array coordinates{0.0f, 0.25f, 0.75f, 1.0f};
            for (std::size_t y = 0; y < 4; ++y) {
                for (std::size_t x = 0; x < 4; ++x) {
                    const float u =
                        filter == ResizeFilter::nearest ? (x < 2 ? 0.0f : 1.0f) : coordinates[x];
                    const float v =
                        filter == ResizeFilter::nearest ? (y < 2 ? 0.0f : 1.0f) : coordinates[y];
                    expected.insert(expected.end(), {0.2f * u, 0.4f * v, 0.8f * u * v,
                                                     0.25f * u + 0.5f * v + 0.25f * u * v});
                }
            }
            expect(test::read(ctx, destination), expected);
        }
        destination.set_size({1, 1});
        cmd.resize(source, destination, {.filter = ResizeFilter::bilinear});
        ctx.submit_and_wait(cmd);
        expect(test::read(ctx, destination), Pixel{0.1f, 0.2f, 0.2f, 0.4375f});

        auto odd_source = ctx.create_image({3, 3});
        const auto odd_pixels = pattern(3, 3);
        test::paint(ctx, odd_source, odd_pixels);
        cmd.resize(odd_source, destination, {.filter = ResizeFilter::nearest});
        ctx.submit_and_wait(cmd);
        expect(test::read(ctx, destination), std::span(odd_pixels).subspan(16, 4));
        // A 3x reduction widens the bilinear tent to three pixels; with edge clamping
        // every source pixel then weighs the same.
        Pixel average{};
        for (std::size_t i = 0; i < odd_pixels.size(); ++i) {
            average[i % 4] += odd_pixels[i] / 9;
        }
        cmd.resize(odd_source, destination, {.filter = ResizeFilter::bilinear});
        ctx.submit_and_wait(cmd);
        expect(test::read(ctx, destination), average);
    });

    test::run("Gaussian blur matches independent convolution with reused dirty workspace", [&] {
        for (const auto dimensions : {std::array{9, 5}, std::array{1, 1}, std::array{1, 5}}) {
            const int width = dimensions[0];
            const int height = dimensions[1];
            auto image = ctx.create_image({width, height});
            auto scratch = ctx.create_image({width, height});
            const auto pixels = pattern(width, height);
            auto cmd = ctx.create_commands(3);
            for (const float sigma : {1.3f, 0.1f, 1e-20f}) {
                test::paint(ctx, image, pixels);
                test::garbage(ctx, scratch, {-99, 88, 77, -66});
                auto options = test::reserve_workspace(
                    ctx, image, GaussianBlurOptions{.radius = 3, .sigma = sigma}, gaussian_blur_requirements);
                // A preceding blur fills the opaque workspace with unrelated values.
                cmd.gaussian_blur(scratch, options);
                ctx.submit_and_wait(cmd);
                cmd.gaussian_blur(image, options);
                ctx.submit_and_wait(cmd);
                expect(test::read(ctx, image), blurred(pixels, width, height, 3, sigma), 1);
            }
            test::paint(ctx, image, pixels);
            cmd.gaussian_blur(image, test::reserve_workspace(
                                         ctx, image, GaussianBlurOptions{.radius = 0, .sigma = 1},
                                         gaussian_blur_requirements));
            ctx.submit_and_wait(cmd);
            expect(test::read(ctx, image), pixels, 0);
        }
    });
    return test::finish();
}
