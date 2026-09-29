#include "test.h"
#include <array>
#include <cstring>
#include <limits>

using namespace wgpupixel;

namespace {
template <class T> std::span<std::uint8_t> bytes_of(std::vector<T>& values) {
    return {reinterpret_cast<std::uint8_t*>(values.data()), values.size() * sizeof(T)};
}
} // namespace

int main() {
    test::run("RGBA8 transfers", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({256, 256});
        auto upload = ctx.create_upload_buffer(image);
        auto readback = ctx.create_readback_buffer(image);
        auto cmd = ctx.create_commands(4);
        std::vector<std::uint8_t> pixels(256 * 256 * 4), result(pixels.size());
        for (unsigned alpha = 0; alpha < 256; ++alpha) {
            for (unsigned value = 0; value < 256; ++value) {
                const auto i = (alpha * 256 + value) * 4;
                pixels[i] = static_cast<std::uint8_t>(value);
                pixels[i + 1] = static_cast<std::uint8_t>(255 - value);
                pixels[i + 2] = static_cast<std::uint8_t>((value * 37) % 256);
                pixels[i + 3] = static_cast<std::uint8_t>(alpha);
            }
        }
        test::run("all byte values and alpha levels roundtrip", [&] {
            ctx.write(upload, pixels);
            cmd.upload(upload, image);
            cmd.download(image, readback);
            ctx.submit_and_wait(cmd);
            ctx.read(readback, result);
            for (std::size_t i = 0; i < pixels.size(); ++i) {
                test::near(result[i], pixels[(i / 4) * 4 + 3] == 0 ? 0 : pixels[i], 0);
            }
        });
        test::run("upload decodes sRGB before linear grayscale", [&] {
            cmd.grayscale(image);
            cmd.download(image, readback);
            ctx.submit_and_wait(cmd);
            ctx.read(readback, result);
            for (std::size_t i = 0; i < pixels.size(); i += 4) {
                double gray = 0;
                for (std::size_t c = 0; c < 3; ++c) {
                    const double v = pixels[i + c] / 255.0;
                    const double linear =
                        v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
                    gray += linear * std::array{0.2126, 0.7152, 0.0722}[c];
                }
                const auto expected =
                    pixels[i + 3] == 0 ? std::uint8_t{0} : test::channel(static_cast<float>(gray));
                for (std::size_t c = 0; c < 3; ++c) {
                    test::near(result[i + c], expected, 1);
                }
                test::near(result[i + 3], pixels[i + 3], 0);
            }
        });
        test::run("logical size snapshots and short rewrites", [&] {
            image.set_size({3, 1});
            const std::array<std::uint8_t, 12> sample{10,  11, 12,  255, 45, 123,
                                                      234, 1,  255, 3,   8,  0};
            ctx.write(upload, sample);
            cmd.upload(upload, image);
            cmd.download(image, readback);
            image.set_size({1, 1});
            ctx.submit_and_wait(cmd);
            std::array<std::uint8_t, 12> output{};
            ctx.read(readback, output);
            for (std::size_t i = 0; i < sample.size(); ++i) {
                test::near(output[i], i < 8 ? sample[i] : 0, 0);
            }
            test::error(ErrorCode::invalid_argument, "read", "pixels",
                        [&] { ctx.read(readback, result); });
        });
        test::run("uploaded alpha is linear coverage when compositing", [&] {
            auto backdrop = ctx.create_image({1, 1});
            const std::array<std::uint8_t, 4> red{255, 0, 0, 128};
            ctx.write(upload, red);
            cmd.upload(upload, image);
            cmd.fill(backdrop, {.color = {0, 0, 0, 1}});
            cmd.blend(image, backdrop, {.position = {0, 0}});
            cmd.download(backdrop, readback);
            ctx.submit_and_wait(cmd);
            std::array<std::uint8_t, 4> output{};
            ctx.read(readback, output);
            test::check(output == std::array<std::uint8_t, 4>{188, 0, 0, 255},
                        "alpha must not receive the sRGB transfer function");
            ctx.destroy(backdrop);
        });
        test::run("HDR and negative values survive processing until export", [&] {
            auto scratch = ctx.create_image({1, 1});
            for (const float value : {1.5f, -0.5f}) {
                cmd.fill(image, {.color = {value, value, value, 1}});
                cmd.gaussian_blur(
                    image, test::reserve_workspace(ctx, image,
                                                   GaussianBlurOptions{.radius = 1, .sigma = 1},
                                                   gaussian_blur_requirements));
                cmd.brightness(image, {.amount = 0.5f - value});
                ctx.submit_and_wait(cmd);
                const auto output = test::read(ctx, image);
                for (std::size_t c = 0; c < 3; ++c) {
                    test::near(output[c], 188, 0);
                }
                test::near(output[3], 255, 0);
            }
            ctx.destroy(scratch);
        });
        test::run("download unpremultiplies and clips only the exported copy", [&] {
            cmd.fill(image, {.color = {-0.5f, 2.0f, 0.25f, 0.5f}});
            cmd.download(image, readback);
            ctx.submit_and_wait(cmd);
            std::array<std::uint8_t, 4> output{};
            ctx.read(readback, output);
            const std::array<std::uint8_t, 4> expected{0, 255, 188, 128};
            for (std::size_t c = 0; c < 4; ++c) {
                test::near(output[c], expected[c], 0);
            }
            cmd.brightness(image, {.amount = 1.5f});
            cmd.download(image, readback);
            ctx.submit_and_wait(cmd);
            ctx.read(readback, output);
            test::near(output[0], 188, 0); // Negative red survived the previous export.
        });
        ctx.destroy(upload);
        ctx.destroy(readback);
        ctx.destroy(image);
    });
    test::run("16-bit and float transfers", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({256, 256});
        auto upload16 = ctx.create_upload_buffer(image, {.format = TransferFormat::rgba16});
        auto readback16 = ctx.create_readback_buffer(image, {.format = TransferFormat::rgba16});
        auto upload32 = ctx.create_upload_buffer(image, {.format = TransferFormat::rgba32_float});
        auto readback32 =
            ctx.create_readback_buffer(image, {.format = TransferFormat::rgba32_float});
        auto readback8 = ctx.create_readback_buffer(image);
        test::check(upload16.bytes_per_pixel() == 8 && readback16.bytes_per_pixel() == 8 &&
                        upload32.bytes_per_pixel() == 16 && readback32.bytes_per_pixel() == 16 &&
                        readback8.bytes_per_pixel() == 4,
                    "bytes_per_pixel must follow the transfer format");
        auto cmd = ctx.create_commands(4);
        test::run("every 16-bit value roundtrips at several alpha levels", [&] {
            for (const std::uint16_t alpha : {65535, 40000, 257, 1, 0}) {
                std::vector<std::uint16_t> pixels(256 * 256 * 4), result(pixels.size());
                for (std::size_t i = 0; i < 65536; ++i) {
                    pixels[4 * i] = static_cast<std::uint16_t>(i);
                    pixels[4 * i + 1] = static_cast<std::uint16_t>(65535 - i);
                    pixels[4 * i + 2] = static_cast<std::uint16_t>(i * 37);
                    pixels[4 * i + 3] = alpha;
                }
                ctx.write(upload16, bytes_of(pixels));
                cmd.upload(upload16, image);
                cmd.download(image, readback16);
                ctx.submit_and_wait(cmd);
                ctx.read(readback16, bytes_of(result));
                for (std::size_t i = 0; i < pixels.size(); ++i) {
                    const auto expected = alpha == 0 ? 0 : pixels[i];
                    test::near(result[i], expected, i % 4 == 3 ? 0 : 1);
                }
            }
        });
        test::run("8-bit values widen exactly to 16-bit", [&] {
            auto upload8 = ctx.create_upload_buffer(image);
            std::vector<std::uint8_t> pixels(256 * 256 * 4);
            for (std::size_t i = 0; i < pixels.size(); ++i) {
                pixels[i] = i % 4 == 3 ? 255 : static_cast<std::uint8_t>(i / 4);
            }
            ctx.write(upload8, pixels);
            cmd.upload(upload8, image);
            cmd.download(image, readback16);
            ctx.submit_and_wait(cmd);
            std::vector<std::uint16_t> result(pixels.size());
            ctx.read(readback16, bytes_of(result));
            for (std::size_t i = 0; i < pixels.size(); ++i) {
                test::near(result[i], pixels[i] * 257.0f, 1);
            }
            ctx.destroy(upload8);
        });
        test::run("float transfers are lossless linear premultiplied storage", [&] {
            std::vector<float> pixels(256 * 256 * 4), result(pixels.size());
            for (std::size_t i = 0; i < pixels.size(); ++i) {
                pixels[i] = (float(i % 1031) - 300.0f) / 97.0f + 1e-7f * float(i % 7);
            }
            pixels[0] = std::numeric_limits<float>::max();
            pixels[1] = -0.0f;
            ctx.write(upload32, bytes_of(pixels));
            cmd.upload(upload32, image);
            cmd.download(image, readback32);
            ctx.submit_and_wait(cmd);
            ctx.read(readback32, bytes_of(result));
            test::check(std::memcmp(result.data(), pixels.data(), pixels.size() * 4) == 0,
                        "float transfer must be bit exact");
            // The same storage exports to RGBA8 by unpremultiplying and encoding sRGB.
            cmd.download(image, readback8);
            ctx.submit_and_wait(cmd);
            std::vector<std::uint8_t> encoded(pixels.size());
            ctx.read(readback8, encoded);
            const auto expected = test::encode(std::span<const float>(pixels).subspan(4));
            for (std::size_t i = 4; i < encoded.size(); ++i) {
                test::near(encoded[i], expected[i - 4], 1);
            }
        });
        for (auto buffer : {upload16, upload32}) {
            ctx.destroy(buffer);
        }
        for (auto buffer : {readback16, readback32, readback8}) {
            ctx.destroy(buffer);
        }
        ctx.destroy(image);
    });

    test::run("region transfers and small buffers", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({7, 5});
        auto cmd = ctx.create_commands(4);
        cmd.fill(image, {.color = {0, 0, 0, 1}});
        auto patch = ctx.create_upload_buffer(image, {.capacity_pixels = 6});
        test::check(patch.capacity_pixels() == 6, "capacity_pixels must size the buffer");
        std::array<std::uint8_t, 24> patch_bytes{};
        for (std::size_t i = 0; i < patch_bytes.size(); ++i) {
            patch_bytes[i] = i % 4 == 3 ? 255 : static_cast<std::uint8_t>(10 * i);
        }
        ctx.write(patch, patch_bytes);
        const Rect area{2, 1, 3, 2};
        cmd.upload(patch, image, {.region = area});
        ctx.submit_and_wait(cmd);
        const auto whole = test::read(ctx, image);
        for (int y = 0; y < 5; ++y) {
            for (int x = 0; x < 7; ++x) {
                const bool inside = x >= 2 && x < 5 && y >= 1 && y < 3;
                for (int c = 0; c < 4; ++c) {
                    const auto expected =
                        inside ? patch_bytes[((y - 1) * 3 + (x - 2)) * 4 + c] : c == 3 ? 255 : 0;
                    test::near(whole[(y * 7 + x) * 4 + c], expected, 0);
                }
            }
        }
        auto tile = ctx.create_readback_buffer(image, {.capacity_pixels = 12});
        cmd.download(image, tile, {.region = Rect{1, 2, 4, 3}});
        ctx.submit_and_wait(cmd);
        std::array<std::uint8_t, 48> tile_bytes{};
        ctx.read(tile, tile_bytes);
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 4; ++x) {
                for (int c = 0; c < 4; ++c) {
                    test::near(tile_bytes[(y * 4 + x) * 4 + c],
                               whole[((y + 2) * 7 + x + 1) * 4 + c], 0);
                }
            }
        }
        test::error(ErrorCode::invalid_argument, "read", "pixels",
                    [&] { ctx.read(tile, std::span(tile_bytes).first(44)); });
        // Eyedropper: one linear pixel without reading back the canvas.
        auto probe = ctx.create_readback_buffer(
            image, {.format = TransferFormat::rgba32_float, .capacity_pixels = 1});
        cmd.fill(image, {.color = {2.5f, -1, 0.25f, 0.5f}, .region = Rect{6, 4, 1, 1}});
        cmd.download(image, probe, {.region = Rect{6, 4, 1, 1}});
        ctx.submit_and_wait(cmd);
        std::array<float, 4> sample{};
        ctx.read(probe, std::span(reinterpret_cast<std::uint8_t*>(sample.data()), 16));
        test::check(sample == std::array{2.5f, -1.0f, 0.25f, 0.5f}, "eyedropper sample changed");

        for (const Rect invalid : {Rect{-1, 0, 1, 1}, Rect{0, 0, 0, 1}, Rect{0, 0, 1, -1},
                                   Rect{6, 0, 2, 1}, Rect{0, 4, 1, 2},
                                   Rect{std::numeric_limits<std::int32_t>::max(), 0, 1, 1}}) {
            test::error(ErrorCode::invalid_argument, "upload", "region",
                        [&] { cmd.upload(patch, image, {.region = invalid}); });
            test::error(ErrorCode::invalid_argument, "download", "region",
                        [&] { cmd.download(image, tile, {.region = invalid}); });
        }
        test::error(ErrorCode::capacity, "upload", "source", [&] { cmd.upload(patch, image); });
        test::error(ErrorCode::capacity, "download", "destination",
                    [&] { cmd.download(image, tile, {.region = Rect{0, 0, 7, 2}}); });
        ctx.write(patch, std::span(patch_bytes).first(8));
        test::error(ErrorCode::invalid_argument, "upload", "source",
                    [&] { cmd.upload(patch, image, {.region = area}); });
        test::error(ErrorCode::invalid_argument, "create_upload_buffer", "format", [&] {
            (void)ctx.create_upload_buffer(image, {.format = static_cast<TransferFormat>(3)});
        });
        test::error(ErrorCode::capacity, "create_readback_buffer", "capacity_pixels", [&] {
            (void)ctx.create_readback_buffer(
                image, {.capacity_pixels = std::numeric_limits<std::uint64_t>::max()});
        });
        auto upload16 = ctx.create_upload_buffer(image, {.format = TransferFormat::rgba16});
        test::error(ErrorCode::invalid_argument, "write", "pixels",
                    [&] { ctx.write(upload16, std::span(patch_bytes).first(4)); });
        cmd.fill(image, {.color = {0, 0, 0, 0}});
        ctx.submit_and_wait(cmd);
        for (auto buffer : {patch, upload16}) {
            ctx.destroy(buffer);
        }
        for (auto buffer : {tile, probe}) {
            ctx.destroy(buffer);
        }
        ctx.destroy(image);
    });
    return test::finish();
}
