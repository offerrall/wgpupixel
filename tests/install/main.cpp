// Build separately against an installation: exported targets, headers and runtimes.
#include <wgpupixel_io.h>
#ifdef WGPUPIXEL_CONSUMER_TEXT
#include <wgpupixel_text.h>
#endif
#include <array>
#include <filesystem>
#include <iostream>

int main() {
    try {
        using namespace wgpupixel;
#ifdef WGPUPIXEL_CONSUMER_TEXT
        text::FontCollection fonts;
        if (!fonts.families().empty()) {
            return 1;
        }
#endif
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        auto cmd = ctx.create_commands();
        cmd.fill(image, {.color = {1, 0, 0, 1}});
        ctx.submit_and_wait(cmd);
        io::save(ctx, image, "installed.png");
        auto decoded = io::load(ctx, "installed.png");
        auto readback = ctx.create_readback_buffer(decoded);
        cmd.download(decoded, readback);
        ctx.submit_and_wait(cmd);
        std::array<std::uint8_t, 4> pixels{};
        ctx.read(readback, pixels);
        std::filesystem::remove("installed.png");
        return pixels == std::array<std::uint8_t, 4>{255, 0, 0, 255} ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
