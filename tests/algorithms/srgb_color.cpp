#include "../test.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

using namespace wgpupixel;

namespace {
// Relative float error of a GPU pow (exp2/log2) plus rounding of the 1/12.92 slope.
void close(float actual, float expected, std::string_view what) {
    test::check(std::isfinite(actual) &&
                    std::abs(actual - expected) <= 4e-6f * std::max(1.0f, std::abs(expected)) + 1e-9f,
                std::string(what) + ": expected " + std::to_string(expected) + ", got " +
                    std::to_string(actual));
}

// IEC 61966-2-1 reference in long double, written as the standard's two branches.
long double decode(long double v) {
    return v <= 0.04045L ? v / 12.92L : std::pow((v + 0.055L) / 1.055L, 2.4L);
}
} // namespace

int main() {
    test::run("from_srgb and to_srgb hit exact values at 0, the 0.04045 knee, 0.5 and 1", [] {
        const auto zero = from_srgb(0, 0, 0);
        test::check(zero.r == 0 && zero.g == 0 && zero.b == 0 && zero.a == 1, "black");
        const auto white = from_srgb(1, 1, 1, 1);
        test::check(white.r == 1 && white.g == 1 && white.b == 1 && white.a == 1, "white");
        // Knee: 0.04045 / 12.92 = 0.00313080495...; both branches meet there to 1e-8.
        const auto knee = from_srgb(0.04045f, 0.04045f, 0.04045f);
        test::near(knee.r, 0.0031308049535603713f, 1e-9f);
        // Just above the knee takes the power branch.
        const auto above = from_srgb(0.0405f, 0, 0);
        test::near(above.r, static_cast<float>(std::pow((0.0405L + 0.055L) / 1.055L, 2.4L)), 1e-9f);
        // Mid gray: ((0.5 + 0.055) / 1.055)^2.4 = 0.21404114048223255.
        const auto half = from_srgb(0.5f, 0.5f, 0.5f);
        test::near(half.r, 0.21404114048223255f, 1e-7f);
        const auto back = to_srgb({0.21404114048223255f, 0.0031308049535603713f, 0, 1});
        test::near(back[0], 0.5f, 1e-7f);
        test::near(back[1], 0.04045f, 1e-7f);
        test::check(back[2] == 0 && back[3] == 1, "to_srgb black/alpha");
        const auto one = to_srgb({1, 1, 1, 1});
        test::check(one[0] == 1 && one[3] == 1, "to_srgb white");
    });

    test::run("helpers premultiply, round trip every 8-bit level and treat alpha 0 as clear", [] {
        for (const float alpha : {1.0f, 0.5f, 0.2f, 1.0f / 255}) {
            for (int level = 0; level < 256; ++level) {
                const float v = level / 255.0f;
                const auto color = from_srgb(v, 1 - v, 0.25f, alpha);
                test::near(color.a, alpha, 0);
                test::near(color.r, static_cast<float>(decode(v) * alpha),
                           1e-7f * std::max(1.0f, color.r));
                test::check(color.r <= color.a && color.g <= color.a, "premultiplied bound");
                const auto srgb = to_srgb(color);
                test::near(srgb[0], v, 2e-6f);
                test::near(srgb[1], 1 - v, 2e-6f);
                test::near(srgb[2], 0.25f, 2e-6f);
                test::near(srgb[3], alpha, 0);
            }
        }
        const auto clear = from_srgb(0.7f, 0.2f, 1, 0);
        test::check(clear.r == 0 && clear.g == 0 && clear.b == 0 && clear.a == 0,
                    "alpha 0 must give transparent black");
        for (const float a : {0.0f, -0.5f}) {
            const auto srgb = to_srgb({0.3f, 0.3f, 0.3f, a});
            test::check(srgb == std::array<float, 4>{0, 0, 0, 0}, "alpha <= 0 must give zeros");
        }
        // Clamped like RGBA8/RGBA16 transfers: out-of-range inputs, HDR and negative outputs.
        const auto over = from_srgb(1.5f, -0.2f, 0.5f, 2);
        test::check(over.r == 1 && over.g == 0 && over.a == 1, "from_srgb clamps");
        const auto hdr = to_srgb({3, -0.5f, 0.5f, 0.5f});
        test::check(hdr[0] == 1 && hdr[1] == 0 && hdr[3] == 0.5f, "to_srgb clamps");
        test::near(hdr[2], 1, 1e-7f);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        test::check(std::isnan(from_srgb(nan, 0, 0).r), "NaN is kept");
    });

    test::run("helpers agree with the GPU rgba8/rgba16 upload and download transfers", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({256, 4});
        // RGBA8: every level, four alphas (one per row).
        constexpr std::array<std::uint8_t, 4> alphas{255, 128, 51, 0};
        std::vector<std::uint8_t> bytes;
        for (const auto a : alphas) {
            for (int i = 0; i < 256; ++i) {
                bytes.insert(bytes.end(), {std::uint8_t(i), std::uint8_t(255 - i),
                                           std::uint8_t(i * 7 % 256), a});
            }
        }
        auto upload = ctx.create_upload_buffer(image);
        ctx.write(upload, bytes);
        ctx.run_and_wait([&](Commands& cmd) { cmd.upload(upload, image); });
        auto gpu = test::read_float(ctx, image);
        for (std::size_t p = 0; p < bytes.size() / 4; ++p) {
            const auto* b = &bytes[4 * p];
            const auto expected = from_srgb(b[0] / 255.0f, b[1] / 255.0f, b[2] / 255.0f, b[3] / 255.0f);
            close(gpu[4 * p], expected.r, "rgba8 r");
            close(gpu[4 * p + 1], expected.g, "rgba8 g");
            close(gpu[4 * p + 2], expected.b, "rgba8 b");
            close(gpu[4 * p + 3], expected.a, "rgba8 a");
        }
        // Download back to RGBA8: to_srgb must round to the byte the GPU writes.
        const auto encoded8 = test::read(ctx, image);
        for (std::size_t p = 0; p < bytes.size() / 4; ++p) {
            const auto srgb = to_srgb({gpu[4 * p], gpu[4 * p + 1], gpu[4 * p + 2], gpu[4 * p + 3]});
            for (int c = 0; c < 4; ++c) {
                test::near(encoded8[4 * p + c], std::floor(srgb[c] * 255 + 0.5f), 0);
                test::near(encoded8[4 * p + c], bytes[4 * p + 3] ? bytes[4 * p + c] : 0, 0);
            }
        }
        ctx.destroy(upload);

        // RGBA16: levels spread over [0, 65535], including the knee neighbours.
        std::vector<std::uint16_t> words;
        for (int row = 0; row < 4; ++row) {
            for (int i = 0; i < 256; ++i) {
                const std::uint16_t v = i == 0 ? 2650 : i == 1 ? 2651 : std::uint16_t(i * 257);
                const std::uint16_t a = row == 3 ? 0 : std::uint16_t(65535 - row * 20000);
                words.insert(words.end(), {v, std::uint16_t(65535 - v), std::uint16_t(v / 3), a});
            }
        }
        auto upload16 = ctx.create_upload_buffer(image, {.format = TransferFormat::rgba16});
        std::vector<std::uint8_t> raw(words.size() * 2);
        std::memcpy(raw.data(), words.data(), raw.size());
        ctx.write(upload16, raw);
        ctx.run_and_wait([&](Commands& cmd) { cmd.upload(upload16, image); });
        gpu = test::read_float(ctx, image);
        for (std::size_t p = 0; p < words.size() / 4; ++p) {
            const auto* w = &words[4 * p];
            const auto expected = from_srgb(w[0] / 65535.0f, w[1] / 65535.0f, w[2] / 65535.0f,
                                            w[3] / 65535.0f);
            close(gpu[4 * p], expected.r, "rgba16 r");
            close(gpu[4 * p + 1], expected.g, "rgba16 g");
            close(gpu[4 * p + 2], expected.b, "rgba16 b");
            close(gpu[4 * p + 3], expected.a, "rgba16 a");
        }
        auto readback16 = ctx.create_readback_buffer(image, {.format = TransferFormat::rgba16});
        ctx.run_and_wait([&](Commands& cmd) { cmd.download(image, readback16); });
        std::vector<std::uint8_t> out(raw.size());
        ctx.read(readback16, out);
        std::vector<std::uint16_t> encoded16(words.size());
        std::memcpy(encoded16.data(), out.data(), out.size());
        for (std::size_t p = 0; p < words.size() / 4; ++p) {
            const auto srgb = to_srgb({gpu[4 * p], gpu[4 * p + 1], gpu[4 * p + 2], gpu[4 * p + 3]});
            for (int c = 0; c < 4; ++c) {
                // One 16-bit step: GPU pow and the quantizer's rounding may differ at a tie.
                test::near(encoded16[4 * p + c], srgb[c] * 65535, 1.0f);
            }
        }
        ctx.destroy(upload16);
        ctx.destroy(readback16);
        ctx.destroy(image);
    });
    return test::finish();
}
