#include "selection_support.h"
#include <cstring>
#include <limits>
using namespace wgpupixel;

int main() {
    test::run(
        "apply_mask scales HDR, signed RGB, tiny and zero alpha with positioned coverage", [] {
            auto ctx = Context::create();
            constexpr int width = 19, height = 13;
            auto matte = ctx.create_mask({width, height});
            auto selection_mask = ctx.create_mask({width, height});
            auto image = ctx.create_image({width, height});
            selection::Bytes bytes(width * height), selection_bytes(width * height);
            std::vector<float> pixels(width * height * 4);
            for (int i = 0; i < width * height; ++i) {
                bytes[i] = std::uint8_t((i * 37) % 256);
                selection_bytes[i] = std::array<std::uint8_t, 3>{0, 128, 255}[i % 3];
                const float alpha = std::array{0.f, 1e-12f, .5f, 1.f}[i % 4];
                pixels[4 * i] = 3 * alpha;
                pixels[4 * i + 1] = -2 * alpha;
                pixels[4 * i + 2] = .25f * alpha;
                pixels[4 * i + 3] = alpha;
            }
            selection::upload(ctx, matte, bytes);
            selection::upload(ctx, selection_mask, selection_bytes);
            auto upload = ctx.create_upload_buffer(image, {.format = TransferFormat::rgba32_float});
            auto download =
                ctx.create_readback_buffer(image, {.format = TransferFormat::rgba32_float});
            ctx.write(upload,
                      {reinterpret_cast<const std::uint8_t*>(pixels.data()), pixels.size() * 4});
            for (int offset :
                 {-2, 0, 3, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
                ctx.run_and_wait([&](Commands& cmd) {
                    cmd.upload(upload, image);
                    cmd.apply_mask(matte, image,
                                   {.position = {offset, 0},
                                    .mask = &selection_mask,
                                    .region = Rect{1, 2, 16, 9}});
                    cmd.download(image, download);
                });
                std::vector<float> result(pixels.size());
                ctx.read(download,
                         {reinterpret_cast<std::uint8_t*>(result.data()), result.size() * 4});
                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        const auto sx = std::int64_t(x) - offset;
                        const int i = y * width + x;
                        double multiplier = 1;
                        if (x >= 1 && x < 17 && y >= 2 && y < 11 && sx >= 0 && sx < width) {
                            multiplier = 1 - selection_bytes[i] / 255.0 *
                                                 (1 - bytes[y * width + sx] / 255.0);
                        }
                        for (int c = 0; c < 4; ++c) {
                            test::near(result[i * 4 + c], float(pixels[i * 4 + c] * multiplier),
                                       std::max(1e-18f, std::abs(pixels[i * 4 + c]) * 2e-6f));
                        }
                    }
                }
            }
            auto commands = ctx.create_commands(1);
            Mask empty;
            test::error(ErrorCode::invalid_resource, "apply_mask", "source",
                        [&] { commands.apply_mask(empty, image); });
            test::error(ErrorCode::invalid_argument, "apply_mask", "region",
                        [&] { commands.apply_mask(matte, image, {.region = Rect{0, 0, -1, 1}}); });
            commands.apply_mask(matte, image);
            ctx.submit_and_wait(commands);
        });
    return test::finish();
}
