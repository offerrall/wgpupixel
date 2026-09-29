#include "../test.h"
#include <array>
#include <cstdint>

using namespace wgpupixel;

namespace {
void verify(Context& ctx, int sw, int sh, int dw, int dh, bool masked = false) {
    auto source = ctx.create_image({sw, sh});
    auto destination = ctx.create_image({dw, dh});
    auto upload = ctx.create_upload_buffer(source);
    std::vector<std::uint8_t> pixels(std::size_t(sw) * sh * 4);
    for (std::size_t i = 0; i < pixels.size() / 4; ++i) {
        // Adjacent pixels always differ; two more channels identify distant
        // samples when testing products that exceed 32 bits.
        pixels[4 * i] = (i & 1) ? 255 : 0;
        pixels[4 * i + 1] = static_cast<std::uint8_t>(i);
        pixels[4 * i + 2] = static_cast<std::uint8_t>(i >> 8);
        pixels[4 * i + 3] = 255;
    }
    auto cmd = ctx.create_commands(3);
    ctx.write(upload, pixels);
    cmd.upload(upload, source);
    ctx.submit_and_wait(cmd);
    Mask mask;
    if (masked) {
        mask = ctx.create_mask({dw, dh});
        cmd.fill(mask, {.coverage = 1.0f});
        cmd.fill(destination, {.color = {1, 0, 1, 1}});
    }
    cmd.resize(source, destination,
               {.filter = ResizeFilter::nearest, .mask = masked ? &mask : nullptr});
    ctx.submit_and_wait(cmd);
    const auto actual = test::read(ctx, destination);
    for (int y = 0; y < dh; ++y) {
        // Independent rational mapping: C++ uint64_t, no floating point.
        const auto sy = ((2 * std::uint64_t(y) + 1) * sh) / (2 * std::uint64_t(dh));
        for (int x = 0; x < dw; ++x) {
            const auto sx = ((2 * std::uint64_t(x) + 1) * sw) / (2 * std::uint64_t(dw));
            const auto from = (sy * sw + sx) * 4;
            const auto to = (std::size_t(y) * dw + x) * 4;
            for (int c = 0; c < 4; ++c) {
                test::check(actual[to + c] == pixels[from + c],
                            "nearest rational mapping mismatch at " + std::to_string(x) + "," +
                                std::to_string(y) + " for " + std::to_string(sw) + "x" +
                                std::to_string(sh) + " -> " + std::to_string(dw) + "x" +
                                std::to_string(dh));
            }
        }
    }
    if (masked) {
        ctx.destroy(mask);
    }
    ctx.destroy(upload);
    ctx.destroy(destination);
    ctx.destroy(source);
}
} // namespace

int main() {
    test::run("nearest resolves near-boundary 4K samples with exact pixel centers", [] {
        auto ctx = Context::create();
        // Old float mapping selected 3823 instead of 3822 at destination x=3710.
        verify(ctx, 3840, 1, 3727, 1);
        verify(ctx, 1, 3840, 1, 3727);
        verify(ctx, 4114, 1, 7175, 1);
        verify(ctx, 3840, 1, 3727, 1, true);
    });
    test::run("nearest identity, singleton axes, exact ties and odd 2D scales", [] {
        auto ctx = Context::create();
        for (auto shape : {std::array{1, 1, 1, 1}, std::array{1, 1, 7, 9}, std::array{2, 1, 41, 1},
                           std::array{41, 1, 2, 1}, std::array{17, 13, 17, 13},
                           std::array{37, 29, 19, 23}, std::array{19, 23, 37, 29}}) {
            verify(ctx, shape[0], shape[1], shape[2], shape[3]);
        }
    });
    test::run("nearest large-axis mapping does not overflow 32-bit products", [] {
        auto ctx = Context::create();
        // About 2 MiB per image; accepted by baseline WebGPU resource limits.
        // Exercise the wide-product path on both axes and both scaling directions.
        verify(ctx, 65535, 1, 32769, 1);
        verify(ctx, 65537, 1, 32768, 1);
        verify(ctx, 131071, 1, 65537, 1);
        verify(ctx, 65537, 1, 131071, 1);
        verify(ctx, 1, 65537, 1, 131071);
    });
    return test::finish();
}
