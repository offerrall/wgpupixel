#include "../resampling.h"
#include <array>
#include <limits>
#include <numbers>
#include <utility>

using namespace wgpupixel;

namespace {
void expect(Context& ctx, const Image& image, std::span<const float> pixels, float degrees,
            ResizeFilter filter) {
    const auto expected = test::encode(pixels);
    const auto actual = test::read(ctx, image);
    for (std::size_t i = 0; i < actual.size(); ++i) {
        // Straight RGB is invisible when both exported alpha bytes are zero.
        const auto alpha = (i / 4) * 4 + 3;
        if (i % 4 != 3 && actual[alpha] == 0 && expected[alpha] == 0) {
            continue;
        }
        test::check(std::abs(int(actual[i]) - int(expected[i])) <= 2,
                    "rotation (degrees=" + std::to_string(degrees) +
                        ", filter=" + std::to_string(std::to_underlying(filter)) +
                        ") differs from CPU reference at " + std::to_string(i) + ": got " +
                        std::to_string(actual[i]) + ", expected " + std::to_string(expected[i]));
    }
}

std::vector<float> rotated(std::span<const float> pixels, int sw, int sh, int dw, int dh,
                           double angle, ResizeFilter filter) {
    const double cosine = std::cos(angle * std::numbers::pi / 180);
    const double sine = std::sin(angle * std::numbers::pi / 180);
    std::vector<float> result(std::size_t(dw) * dh * 4);
    for (int y = 0; y < dh; ++y) {
        for (int x = 0; x < dw; ++x) {
            const double dx = x - (dw - 1) * 0.5, dy = y - (dh - 1) * 0.5;
            const double sx = (sw - 1) * 0.5 + dx * cosine + dy * sine;
            const double sy = (sh - 1) * 0.5 - dx * sine + dy * cosine;
            const auto pixel = test::transparent_sample(pixels, sw, sh, sx, sy, filter);
            std::copy(pixel.begin(), pixel.end(), result.begin() + (y * dw + x) * 4);
        }
    }
    return result;
}
} // namespace

int main() {
    test::run("rotate exact clockwise non-square quarter turns for every filter", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({3, 2});
        auto destination = ctx.create_image({2, 3});
        std::vector<float> pixels;
        for (int i = 0; i < 6; ++i) {
            pixels.insert(pixels.end(), {i * 0.04f, 0.1f, 0.2f, 0.5f});
        }
        test::paint(ctx, source, pixels);
        std::vector<float> expected(24);
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 2; ++x) {
                for (int c = 0; c < 4; ++c) {
                    expected[(y * 2 + x) * 4 + c] = pixels[((1 - x) * 3 + y) * 4 + c];
                }
            }
        }
        for (float angle : {90.f, -270.f, 450.f}) {
            for (auto filter : {ResizeFilter::nearest, ResizeFilter::bilinear,
                                ResizeFilter::bicubic, ResizeFilter::lanczos}) {
                auto cmd = ctx.create_commands(1);
                cmd.rotate(source, destination, {.degrees = angle, .filter = filter});
                ctx.submit_and_wait(cmd);
                expect(ctx, destination, expected, angle, filter);
            }
        }
    });
    test::run("rotate arbitrary canvas, premultiplied interpolation and transparent outside", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({5, 3});
        auto destination = ctx.create_image({7, 5});
        std::vector<float> pixels;
        for (int i = 0; i < 15; ++i) {
            const float a = (i % 4) * 0.25f;
            pixels.insert(pixels.end(), {a * (i % 5) * 0.2f, a * 0.3f, a * 0.8f, a});
        }
        test::paint(ctx, source, pixels);
        for (auto filter : {ResizeFilter::nearest, ResizeFilter::bilinear, ResizeFilter::bicubic,
                            ResizeFilter::lanczos}) {
            auto cmd = ctx.create_commands(2);
            cmd.fill(destination, {.color = {1, 1, 1, 1}});
            cmd.rotate(source, destination, {.degrees = 31, .filter = filter});
            ctx.submit_and_wait(cmd);
            expect(ctx, destination, rotated(pixels, 5, 3, 7, 5, 31, filter), 31, filter);
        }
    });
    test::run("rotate filters opaque borders without changing straight color", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({32, 32});
        auto destination = ctx.create_image({48, 48});
        const std::array<float, 4> color{1, .25f, .1f, 1};
        std::vector<float> pixels;
        for (int i = 0; i < 32 * 32; ++i) {
            pixels.insert(pixels.end(), color.begin(), color.end());
        }
        auto cmd = ctx.create_commands(2);
        cmd.fill(source, {.color = {color[0], color[1], color[2], color[3]}});
        ctx.submit_and_wait(cmd);
        for (auto filter : {ResizeFilter::nearest, ResizeFilter::bilinear, ResizeFilter::bicubic,
                            ResizeFilter::lanczos}) {
            for (float angle : {15.f, 30.f, 45.f}) {
                cmd.fill(destination, {.color = {0, 1, 0, 1}});
                cmd.rotate(source, destination, {.degrees = angle, .filter = filter});
                ctx.submit_and_wait(cmd);
                expect(ctx, destination, rotated(pixels, 32, 32, 48, 48, angle, filter), angle,
                       filter);
                const auto actual = test::read(ctx, destination);
                int partial = 0;
                for (std::size_t i = 3; i < actual.size(); i += 4) {
                    partial += actual[i] > 0 && actual[i] < 255;
                }
                test::check((partial > 0) == (filter != ResizeFilter::nearest),
                            "only interpolated rotation should have partial edge coverage");
            }
        }
        test::check(test::read(ctx, source) == test::encode(pixels), "rotation changed its source");
    });
    test::run("rotate keeps HDR and validates atomically", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({1, 1});
        auto destination = ctx.create_image({1, 1});
        const std::array<float, 4> pixel{0.75f, 1.f, 0.9f, 0.5f};
        test::paint(ctx, source, pixel);
        auto cmd = ctx.create_commands(2);
        test::error(ErrorCode::invalid_argument, "rotate", "degrees", [&] {
            cmd.rotate(source, destination, {.degrees = std::numeric_limits<float>::infinity()});
        });
        test::error(ErrorCode::invalid_argument, "rotate", "filter", [&] {
            cmd.rotate(source, destination,
                       {.degrees = 0, .filter = static_cast<ResizeFilter>(99)});
        });
        test::error(ErrorCode::invalid_argument, "rotate", "destination",
                    [&] { cmd.rotate(source, source, {.degrees = 0}); });
        cmd.rotate(source, destination, {.degrees = 360});
        cmd.brightness(destination, {.amount = -1});
        ctx.submit_and_wait(cmd);
        expect(ctx, destination, std::array<float, 4>{0.25f, 0.5f, 0.4f, 0.5f}, 360,
               ResizeFilter::bilinear);
    });
    return test::finish();
}
