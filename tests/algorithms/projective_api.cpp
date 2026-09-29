#include "selection_support.h"
#include <cstring>
using namespace wgpupixel;

int main() {
    test::run("projective composition has exact geometric translation and scale invariance", [] {
        auto ctx = Context::create();
        constexpr int w = 19, h = 17;
        auto source = ctx.create_mask({w, h}), destination = ctx.create_mask({w, h});
        auto control = ctx.create_mask({w, h});
        selection::Bytes input(w * h), cover(w * h, 255);
        for (int i = 0; i < w * h; ++i) {
            input[i] = std::uint8_t(i % 256);
        }
        cover[3 * w + 4] = 0;
        selection::upload(ctx, source, input);
        selection::upload(ctx, control, cover);
        for (double scalar : {1.0, -7.0, 1e-100, 1e100}) {
            Homography matrix =
                Homography::from(Affine::translate(2, 1)) * Homography::from(Affine::scale(1));
            for (auto& value : matrix.m) {
                value *= scalar;
            }
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.fill(destination, {.coverage = 1});
                cmd.transform(source, destination,
                              ProjectiveTransformOptions{.matrix = matrix,
                                                         .filter = ResizeFilter::nearest,
                                                         .mask = &control,
                                                         .region = Rect{1, 2, 16, 13}});
            });
            selection::Bytes expected(w * h, 255);
            for (int y = 2; y < 15; ++y) {
                for (int x = 1; x < 17; ++x) {
                    if (cover[y * w + x]) {
                        expected[y * w + x] = x < 2 ? 0 : input[(y - 1) * w + x - 2];
                    }
                }
            }
            selection::expect(selection::read(ctx, destination), expected, 0,
                              "composed matrix placement");
        }
        auto commands = ctx.create_commands(1);
        Homography singular{{0, 0, 0, 0, 0, 0, 0, 0, 1}};
        test::error(ErrorCode::invalid_argument, "transform", "matrix", [&] {
            commands.transform(source, destination, ProjectiveTransformOptions{.matrix = singular});
        });
        Homography horizon{{1, 0, 0, 0, 1, 0, -.1, 0, 1}};
        test::error(ErrorCode::invalid_argument, "transform", "matrix", [&] {
            commands.transform(source, destination, ProjectiveTransformOptions{.matrix = horizon});
        });
        commands.copy(source, destination);
        ctx.submit_and_wait(commands);
    });
    test::run("image projective transform keeps HDR and alpha and obeys perspective geometry", [] {
        auto ctx = Context::create();
        constexpr int w = 19, h = 17;
        auto source = ctx.create_image({w, h}), destination = ctx.create_image({w, h});
        auto upload = ctx.create_upload_buffer(source, {.format = TransferFormat::rgba32_float});
        auto download =
            ctx.create_readback_buffer(destination, {.format = TransferFormat::rgba32_float});
        std::vector<float> pixels(w * h * 4), actual(pixels.size());
        for (int i = 0; i < w * h; ++i) {
            const float a = std::array{0.f, 1e-12f, .5f, 1.f}[i % 4];
            pixels[4 * i] = a * (i % w);
            pixels[4 * i + 1] = -a;
            pixels[4 * i + 2] = 2 * a;
            pixels[4 * i + 3] = a;
        }
        ctx.write(upload,
                  {reinterpret_cast<const std::uint8_t*>(pixels.data()), pixels.size() * 4});
        // x' = x/(1+x/32), y' = y/(1+x/32); inverse solved algebraically.
        const Homography matrix{{1, 0, 0, 0, 1, 0, 1.0 / 32, 0, 1}};
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.upload(upload, source);
            cmd.transform(
                source, destination,
                ProjectiveTransformOptions{.matrix = matrix, .filter = ResizeFilter::nearest});
            cmd.download(destination, download);
        });
        ctx.read(download, {reinterpret_cast<std::uint8_t*>(actual.data()), actual.size() * 4});
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const double denom = 1 - (x + .5) / 32;
                const int sx = int(std::floor((x + .5) / denom)),
                          sy = int(std::floor((y + .5) / denom));
                for (int c = 0; c < 4; ++c) {
                    test::near(actual[(y * w + x) * 4 + c],
                               sx < w && sy < h ? pixels[(sy * w + sx) * 4 + c] : 0.f, 1e-6f);
                }
            }
        }
    });
    test::run("mask zoom uses continuous centers and exact nearest pixel blocks", [] {
        auto ctx = Context::create();
        auto source = ctx.create_mask({19, 17}), destination = ctx.create_mask({38, 34});
        selection::Bytes input(19 * 17);
        for (int i = 0; i < 19 * 17; ++i) {
            input[i] = std::uint8_t(i % 256);
        }
        selection::upload(ctx, source, input);
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.zoom(source, destination,
                     {.factor = 2, .center = {9.5f, 8.5f}, .filter = ResizeFilter::nearest});
        });
        selection::Bytes expected(38 * 34);
        for (int y = 0; y < 34; ++y) {
            for (int x = 0; x < 38; ++x) {
                expected[y * 38 + x] = input[(y / 2) * 19 + x / 2];
            }
        }
        selection::expect(selection::read(ctx, destination), expected, 0, "2x2 nearest blocks");
    });
    test::run("mask zoom filters continuous coverage and honors write controls", [] {
        auto ctx = Context::create();
        constexpr int w = 19, h = 17;
        auto source = ctx.create_mask({w, h}), destination = ctx.create_mask({w, h});
        auto control = ctx.create_mask({w, h});
        selection::Bytes input(w * h), coverage(w * h), expected(w * h, 255);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                input[y * w + x] = std::uint8_t(8 * x + 3 * y);
                coverage[y * w + x] = std::array<std::uint8_t, 3>{0, 128, 255}[x % 3];
            }
        }
        selection::upload(ctx, source, input);
        selection::upload(ctx, control, coverage);
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.fill(destination, {.coverage = 1});
            cmd.zoom(source, destination,
                     {.center = {9.75f, 8.5f}, .mask = &control, .region = Rect{1, 2, 16, 13}});
        });
        // A quarter-pixel shift along an 8-byte-per-pixel ramp raises coverage by 2.
        for (int y = 2; y < 15; ++y) {
            for (int x = 1; x < 17; ++x) {
                const int i = y * w + x;
                expected[i] = std::uint8_t(
                    (255 * (255 - coverage[i]) + (input[i] + 2) * coverage[i] + 127) / 255);
            }
        }
        selection::expect(selection::read(ctx, destination), expected, 0, "bilinear ramp");
        auto cmd = ctx.create_commands(1);
        test::error(ErrorCode::invalid_argument, "zoom", "factor",
                    [&] { cmd.zoom(source, destination, {.factor = 0}); });
        test::error(ErrorCode::invalid_argument, "zoom", "destination",
                    [&] { cmd.zoom(source, source, {}); });
        cmd.copy(source, destination);
        ctx.submit_and_wait(cmd);
    });
    return test::finish();
}
