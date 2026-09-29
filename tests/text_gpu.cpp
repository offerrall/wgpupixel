#include "test.h"
#include <wgpupixel_text.h>
#include <filesystem>

using namespace wgpupixel;
int main() {
    test::run("CPU glyph coverage composes translucent text over initialized GPU image", [] {
        text::FontCollection fonts;
        (void)fonts.add_file(
            (std::filesystem::path(WGPUPIXEL_TEST_DATA) / "fonts/NotoSans-Regular.ttf").string());
        text::TextStyle style;
        style.family = "Noto Sans";
        style.size_px = 36;
        const auto raster = fonts.layout("Alpha", style).rasterize(text::RasterFormat::a8);
        test::check(raster.width && raster.height, "empty glyph raster");
        auto ctx = Context::create();
        auto mask = ctx.create_mask({raster.width, raster.height});
        auto layer = ctx.create_image({raster.width, raster.height});
        auto canvas = ctx.create_image({raster.width + 9, raster.height + 7});
        auto upload = ctx.create_upload_buffer(mask);
        ctx.write(upload, raster.pixels);
        auto commands = ctx.create_commands(5);
        commands.upload(upload, mask);
        commands.fill(layer, {.color = {0, 0, 0, 0}});
        commands.fill(layer, {.color = {0.5f, 0, 0, 0.5f}, .mask = &mask});
        commands.fill(canvas, {.color = {0, 0, 0.25f, 0.25f}});
        commands.blend(layer, canvas, {.position = {4, 3}});
        ctx.submit_and_wait(commands);
        auto output = test::read(ctx, canvas);
        std::vector<float> expected(std::size_t(canvas.size().width) * canvas.size().height * 4);
        bool antialiased = false;
        for (std::uint32_t y = 0; y < canvas.size().height; ++y) {
            for (std::uint32_t x = 0; x < canvas.size().width; ++x) {
                float coverage = 0;
                if (x >= 4 && x < raster.width + 4 && y >= 3 && y < raster.height + 3) {
                    coverage = raster.pixels[std::size_t(y - 3) * raster.width + x - 4] / 255.0f;
                }
                antialiased |= coverage > 0 && coverage < 1;
                const float alpha = 0.5f * coverage;
                const auto i = (std::size_t(y) * canvas.size().width + x) * 4;
                expected[i] = alpha;
                expected[i + 2] = 0.25f * (1 - alpha);
                expected[i + 3] = alpha + 0.25f * (1 - alpha);
            }
        }
        test::check(antialiased, "fixture did not exercise fractional glyph coverage");
        auto encoded = test::encode(expected);
        for (std::size_t i = 0; i < output.size(); ++i) {
            test::near(output[i], encoded[i], 1);
        }
        ctx.destroy(upload);
        ctx.destroy(mask);
        ctx.destroy(layer);
        ctx.destroy(canvas);
    });
    test::run("straight-alpha colored text raster round-trips through GPU upload", [] {
        text::FontCollection fonts;
        (void)fonts.add_file(
            (std::filesystem::path(WGPUPIXEL_TEST_DATA) / "fonts/NotoSans-Regular.ttf").string());
        text::TextStyle style;
        style.family = "Noto Sans";
        style.size_px = 32;
        style.color = {0.125f, 0.25f, 0.5f, 0.5f};
        const auto raster = fonts.layout("RGBA", style).rasterize();
        auto ctx = Context::create();
        auto image = ctx.create_image({raster.width, raster.height});
        auto upload = ctx.create_upload_buffer(image);
        ctx.write(upload, raster.pixels);
        auto commands = ctx.create_commands(1);
        commands.upload(upload, image);
        ctx.submit_and_wait(commands);
        const auto output = test::read(ctx, image);
        for (std::size_t i = 0; i < output.size(); i += 4) {
            test::near(output[i + 3], raster.pixels[i + 3], 1);
            if (raster.pixels[i + 3]) {
                for (unsigned c = 0; c < 3; ++c) {
                    test::near(output[i + c], raster.pixels[i + c], 1);
                }
            }
        }
        ctx.destroy(upload);
        ctx.destroy(image);
    });
    return test::finish();
}
