#include "../resampling.h"
#include <array>
#include <limits>
using namespace wgpupixel;

namespace {
std::vector<float> reference(std::span<const float> pixels, int sw, int sh, int dw, int dh,
                             double factor, double cx, double cy, ResizeFilter filter) {
    std::vector<float> result(std::size_t(dw) * dh * 4);
    for (int y = 0; y < dh; ++y) {
        for (int x = 0; x < dw; ++x) {
            const double sx = cx + (x + 0.5 - dw * 0.5) / factor,
                         sy = cy + (y + 0.5 - dh * 0.5) / factor;
            const auto pixel = test::transparent_sample(pixels, sw, sh, sx, sy, filter);
            std::copy(pixel.begin(), pixel.end(), result.begin() + (y * dw + x) * 4);
        }
    }
    return result;
}
void expect(Context& ctx, const Image& image, std::span<const float> pixels) {
    auto actual = test::read(ctx, image);
    const auto expected = test::encode(pixels);
    for (std::size_t i = 0; i < actual.size(); ++i) {
        test::check(std::abs(int(actual[i]) - int(expected[i])) <= 2,
                    "zoom differs from CPU reference at " + std::to_string(i));
    }
}
} // namespace

int main() {
    test::run("zoom all filters identity, off-center canvas, enlargement and reduction", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({5, 3});
        std::vector<float> pixels;
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 5; ++x) {
                const float a = 0.25f + 0.25f * ((x + y) % 4);
                pixels.insert(pixels.end(), {a * x / 4, a * y / 2, a * 0.3f, a});
            }
        }
        test::paint(ctx, source, pixels);
        for (auto filter : {ResizeFilter::nearest, ResizeFilter::bilinear, ResizeFilter::bicubic,
                            ResizeFilter::lanczos}) {
            for (const auto settings :
                 {std::array<float, 5>{5, 3, 1, 2, 1}, std::array<float, 5>{7, 5, 1.7f, 1.2f, 0.7f},
                  std::array<float, 5>{3, 7, 0.6f, 2, 1}}) {
                const int dw = int(settings[0]), dh = int(settings[1]);
                auto dest = ctx.create_image({dw, dh});
                auto cmd = ctx.create_commands(2);
                cmd.fill(dest, {.color = {1, 1, 1, 1}});
                cmd.zoom(source, dest,
                         {.factor = settings[2],
                          .center = {(settings[3]) + 0.5f, (settings[4]) + 0.5f},
                          .filter = filter});
                ctx.submit_and_wait(cmd);
                expect(
                    ctx, dest,
                    reference(pixels, 5, 3, dw, dh, settings[2], settings[3], settings[4], filter));
                ctx.destroy(dest);
            }
        }
    });
    test::run("zoom filters singleton edges and corners over transparent exterior", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({1, 1});
        auto dest = ctx.create_image({1, 1});
        const std::array<float, 4> pixel{.25f, .125f, 0, .5f};
        test::paint(ctx, source, pixel);
        auto cmd = ctx.create_commands(2);
        for (auto filter : {ResizeFilter::nearest, ResizeFilter::bilinear, ResizeFilter::bicubic,
                            ResizeFilter::lanczos}) {
            for (float x : {-.75f, -.5f, -.25f, 0.f, .25f, .5f, .75f, 1.25f, 2.25f, 3.f}) {
                for (float y : {0.f, -.75f}) {
                    cmd.fill(dest, {.color = {0, 1, 0, 1}});
                    cmd.zoom(source, dest,
                             {.factor = 1, .center = {(x) + 0.5f, (y) + 0.5f}, .filter = filter});
                    ctx.submit_and_wait(cmd);
                    expect(ctx, dest, reference(pixel, 1, 1, 1, 1, 1, x, y, filter));
                }
            }
        }
        // Only one quarter of each bilinear axis contributes, including outside
        // the pixel footprint: alpha = .5 * .25 * .25.
        cmd.zoom(source, dest, {.factor = 1, .center = {(-.75f) + 0.5f, (-.75f) + 0.5f}});
        ctx.submit_and_wait(cmd);
        expect(ctx, dest, std::array<float, 4>{.015625f, .0078125f, 0, .03125f});
        test::check(test::read(ctx, source) == test::encode(pixel), "zoom changed its source");
    });
    test::run("zoom masked border blends with the initialized destination", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({1, 1});
        auto dest = ctx.create_image({3, 1});
        auto mask = ctx.create_mask({3, 1});
        auto upload = ctx.create_upload_buffer(mask);
        ctx.write(upload, std::array<std::uint8_t, 3>{0, 128, 255});
        auto cmd = ctx.create_commands(4);
        cmd.upload(upload, mask);
        cmd.fill(source, {.color = {1, 0, 0, 1}});
        cmd.fill(dest, {.color = {0, 1, 0, 1}});
        cmd.zoom(source, dest,
                 {.factor = 1,
                  .center = {(-.25f) + 0.5f, (0) + 0.5f},
                  .filter = ResizeFilter::bilinear,
                  .mask = &mask});
        ctx.submit_and_wait(cmd);
        const float m = 128.f / 255;
        expect(
            ctx, dest,
            std::array<float, 12>{0, 1, 0, 1, .75f * m, 1 - m, 0, 1 - .25f * m, .25f, 0, 0, .25f});
    });
    test::run("zoom invalid arguments preserve recording and HDR remains available", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({1, 1});
        auto dest = ctx.create_image({1, 1});
        test::paint(ctx, source, std::array<float, 4>{0.75f, 1, 0.9f, 0.5f});
        auto cmd = ctx.create_commands(2);
        for (float factor : {0.f, -1.f, std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::denorm_min()}) {
            test::error(ErrorCode::invalid_argument, "zoom", "factor", [&] {
                cmd.zoom(source, dest, {.factor = factor, .center = {(0) + 0.5f, (0) + 0.5f}});
            });
        }
        test::error(ErrorCode::invalid_argument, "zoom", "center", [&] {
            cmd.zoom(source, dest,
                     {.factor = 1,
                      .center = {(std::numeric_limits<float>::infinity()) + 0.5f, (0) + 0.5f}});
        });
        test::error(ErrorCode::invalid_argument, "zoom", "center", [&] {
            cmd.zoom(source, dest,
                     {.factor = 1,
                      .center = {(0) + 0.5f, (std::numeric_limits<float>::quiet_NaN()) + 0.5f}});
        });
        test::error(ErrorCode::invalid_argument, "zoom", "filter", [&] {
            cmd.zoom(source, dest,
                     {.factor = 1,
                      .center = {(0) + 0.5f, (0) + 0.5f},
                      .filter = static_cast<ResizeFilter>(99)});
        });
        test::error(ErrorCode::invalid_argument, "zoom", "destination", [&] {
            cmd.zoom(source, source, {.factor = 1, .center = {(0) + 0.5f, (0) + 0.5f}});
        });
        cmd.zoom(
            source, dest,
            {.factor = 2, .center = {(0) + 0.5f, (0) + 0.5f}, .filter = ResizeFilter::lanczos});
        cmd.brightness(dest, {.amount = -1});
        ctx.submit_and_wait(cmd);
        expect(ctx, dest, std::array<float, 4>{0.25f, 0.5f, 0.4f, 0.5f});
        auto outside = ctx.create_commands(1);
        outside.zoom(source, dest, {.factor = 1, .center = {(1e30f) + 0.5f, (-1e30f) + 0.5f}});
        ctx.submit_and_wait(outside);
        expect(ctx, dest, std::array<float, 4>{0, 0, 0, 0});
        auto large = ctx.create_image({9, 1});
        auto invalid = ctx.create_commands(1);
        test::error(ErrorCode::invalid_argument, "zoom", "factor", [&] {
            invalid.zoom(source, large, {.factor = 1e-38f, .center = {(0) + 0.5f, (0) + 0.5f}});
        });
    });
    return test::finish();
}
