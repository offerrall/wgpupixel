#include "selection_support.h"
#include <limits>
using namespace wgpupixel;
using selection::Bytes;

int main() {
    test::run("localized mask edits preserve packed neighbors and blend coverage", [] {
        auto ctx = Context::create();
        constexpr int w = 19, h = 17;
        auto target = ctx.create_mask({w, h}), source = ctx.create_mask({w, h}),
             control = ctx.create_mask({w, h});
        Bytes initial(w * h), incoming(w * h), coverage(w * h);
        for (int i = 0; i < w * h; ++i) {
            initial[i] = std::uint8_t(i % 256);
            incoming[i] = std::uint8_t(255 - i % 256);
            coverage[i] = std::array<std::uint8_t, 3>{0, 128, 255}[i % 3];
        }
        selection::upload(ctx, source, incoming);
        selection::upload(ctx, control, coverage);
        for (int operation = 0; operation < 6; ++operation) {
            selection::upload(ctx, target, initial);
            const Rect area{-2, 2, 18, 13};
            ctx.run_and_wait([&](Commands& cmd) {
                switch (operation) {
                case 0:
                    cmd.copy(source, target, {.mask = &control, .region = area});
                    break;
                case 1:
                    cmd.invert(target, {.mask = &control, .region = area});
                    break;
                case 2:
                    cmd.fill(target, {.coverage = 1, .mask = &control, .region = area});
                    break;
                case 3:
                    cmd.threshold(target, {.value = .5f, .mask = &control, .region = area});
                    break;
                case 4:
                    cmd.levels(target, {.transfer = {.output_black = 1, .output_white = 0},
                                        .mask = &control,
                                        .region = area});
                    break;
                case 5:
                    cmd.combine(
                        source, target,
                        {.mode = SelectionMode::intersect, .mask = &control, .region = area});
                    break;
                }
            });
            Bytes expected = initial;
            for (int y = 2; y < 15; ++y) {
                for (int x = 0; x < 16; ++x) {
                    const int i = y * w + x;
                    const int after = operation == 0                     ? incoming[i]
                                      : operation == 1 || operation == 4 ? 255 - initial[i]
                                      : operation == 2                   ? 255
                                      : operation == 3 ? (initial[i] >= 128 ? 255 : 0)
                                                       : std::min(initial[i], incoming[i]);
                    // Exact integer interpolation and round-to-nearest of byte coverage.
                    expected[i] = std::uint8_t(
                        (int(initial[i]) * (255 - coverage[i]) + after * coverage[i] + 127) / 255);
                }
            }
            selection::expect(selection::read(ctx, target), expected, 0, "localized mask edit", w);
        }
        auto cmd = ctx.create_commands(1);
        Mask empty;
        test::error(ErrorCode::invalid_resource, "invert", "mask",
                    [&] { cmd.invert(target, {.mask = &empty}); });
        test::error(ErrorCode::invalid_argument, "fill", "mask",
                    [&] { cmd.fill(target, {.coverage = 1, .mask = &target}); });
        test::error(ErrorCode::invalid_argument, "copy", "region",
                    [&] { cmd.copy(source, target, {.region = Rect{0, 0, -1, 2}}); });
        test::error(ErrorCode::invalid_argument, "levels", "transfer.gamma",
                    [&] { cmd.levels(target, {.transfer = {.gamma = 10}}); });
        test::error(ErrorCode::invalid_argument, "levels", "transfer.input_white", [&] {
            cmd.levels(target, {.transfer = {.input_black = .8f, .input_white = .4f}});
        });
        cmd.fill(target, {.coverage = 1});
        ctx.submit_and_wait(cmd);
        selection::expect(selection::read(ctx, target), Bytes(w * h, 255), 0,
                          "failed operations record nothing");
    });
    test::run("A8 patches support arbitrary byte alignment and explicit transfer capacity", [] {
        auto ctx = Context::create();
        constexpr int w = 19, h = 17;
        auto mask = ctx.create_mask({w, h});
        auto upload = ctx.create_upload_buffer(mask, {.capacity_pixels = 15});
        auto readback = ctx.create_readback_buffer(mask, {.capacity_pixels = 15});
        test::check(upload.capacity_pixels() == 15 && readback.capacity_pixels() == 15 &&
                        upload.bytes_per_pixel() == 1,
                    "A8 capacity");
        Bytes patch(15);
        for (int i = 0; i < 15; ++i) {
            patch[i] = std::uint8_t(i * 17);
        }
        ctx.write(upload, patch);
        for (int x = 0; x < 4; ++x) {
            Bytes expected(w * h, 73);
            selection::upload(ctx, mask, expected);
            const Rect region{x, 3, 5, 3};
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.upload(upload, mask, {.region = region});
                cmd.download(mask, readback, {.region = region});
            });
            Bytes actual(15);
            ctx.read(readback, actual);
            selection::expect(actual, patch, 0, "packed patch roundtrip");
            for (int y = 0; y < 3; ++y) {
                for (int j = 0; j < 5; ++j) {
                    expected[(y + 3) * w + x + j] = patch[y * 5 + j];
                }
            }
            selection::expect(selection::read(ctx, mask), expected, 0, "patch neighbors untouched");
        }
        auto cmd = ctx.create_commands(1);
        test::error(ErrorCode::capacity, "upload", "source", [&] { cmd.upload(upload, mask); });
        test::error(ErrorCode::capacity, "download", "destination",
                    [&] { cmd.download(mask, readback); });
        test::error(ErrorCode::invalid_argument, "upload", "region",
                    [&] { cmd.upload(upload, mask, {.region = Rect{-1, 0, 5, 3}}); });
        cmd.download(mask, readback, {.region = Rect{1, 3, 5, 3}});
        ctx.submit_and_wait(cmd);
    });
    return test::finish();
}
