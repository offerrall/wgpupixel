#include "test.h"
#include <wgpupixel_io.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace wgpupixel;
namespace fs = std::filesystem;

namespace {
std::string fixture(std::string_view name) {
    return (fs::path(WGPUPIXEL_TEST_DATA) / name).string();
}

struct Directory {
    fs::path path;
    Directory() {
        const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned i = 0; i < 100; ++i) {
            path = fs::temp_directory_path() /
                   ("wgpupixel-io-" + std::to_string(seed) + "-" + std::to_string(i));
            if (fs::create_directory(path)) {
                return;
            }
        }
        throw std::runtime_error("cannot create temporary directory");
    }
    ~Directory() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
    std::string file(std::string_view name) const {
        return (path / name).string();
    }
};

void pixels(Context& ctx, const Image& image, std::initializer_list<std::uint8_t> expected) {
    test::check(test::read(ctx, image) == std::vector<std::uint8_t>(expected),
                "unexpected RGBA8 pixels");
}
} // namespace

int main() {
    test::run("native file IO", [] {
        Directory files;
        auto ctx = Context::create();
        test::run("RGB, grayscale and 16 bit normalization", [&] {
            auto rgb = io::load(ctx, fixture("rgb.png"));
            pixels(ctx, rgb, {255, 0, 0, 255, 0, 255, 0, 255});
            auto gray = io::load(ctx, fixture("gray.png"));
            pixels(ctx, gray, {0, 0, 0, 255, 128, 128, 128, 255, 255, 255, 255, 255});
            auto wide = io::load(ctx, fixture("wide.png"));
            pixels(ctx, wide, {255, 0, 0, 255, 128, 128, 128, 255});
            ctx.destroy(rgb);
            ctx.destroy(gray);
            ctx.destroy(wide);
        });
        test::run("RGBA ICC and PNG roundtrip", [&] {
            auto image = io::load(ctx, fixture("rgba.png"));
            pixels(ctx, image, {12, 34, 56, 128, 255, 0, 0, 255, 0, 0, 0, 0});
            io::save(ctx, image, files.file("output.PNG"));
            std::ifstream encoded(files.file("output.PNG"), std::ios::binary);
            const std::string data((std::istreambuf_iterator<char>(encoded)), {});
            test::check(data.find("sRGB") != std::string::npos, "PNG lacks sRGB declaration");
            auto loaded = io::load(ctx, files.file("output.PNG"));
            test::check(loaded.size().width == 3 && loaded.size().height == 1, "wrong dimensions");
            test::check(test::read(ctx, loaded) == test::read(ctx, image),
                        "roundtrip altered pixels");
            ctx.destroy(loaded);
            ctx.destroy(image);
        });
        test::run("Display P3 converts to sRGB", [&] {
            auto image = io::load(ctx, fixture("p3.png"));
            const auto actual = test::read(ctx, image);
            const std::array<std::uint8_t, 4> expected{200, 80, 30, 255};
            for (std::size_t i = 0; i < expected.size(); ++i) {
                test::near(actual[i], expected[i], 2);
            }
            ctx.destroy(image);
        });
        test::run("grayscale and 16 bit ICC preserve alpha", [&] {
            auto gray = io::load(ctx, fixture("gray-icc.png"));
            const auto samples = test::read(ctx, gray);
            for (std::size_t i = 0; i < 3; ++i) {
                test::near(samples[i], 129, 1);
            }
            test::check(samples[3] == 128, "ICC changed grayscale alpha");
            auto wide = io::load(ctx, fixture("wide-icc.png"));
            pixels(ctx, wide, {128, 64, 255, 128});
            ctx.destroy(gray);
            ctx.destroy(wide);
            test::error(ErrorCode::io_failed, "io.load", "path",
                        [&] { (void)io::load(ctx, fixture("pq.png")); });
        });
        test::run("JPEG WebP and TIFF decode", [&] {
            for (const auto* name : {"rgb.jpg", "rgb.webp", "rgb.tiff"}) {
                auto image = io::load(ctx, fixture(name));
                test::check(image.size().width == 4 && image.size().height == 4,
                            "wrong dimensions");
                const auto actual = test::read(ctx, image);
                const std::array<std::uint8_t, 4> expected{160, 80, 40, 255};
                for (std::size_t i = 0; i < actual.size(); ++i) {
                    test::near(actual[i], expected[i % 4], 2);
                }
                ctx.destroy(image);
            }
        });
        test::run("linear EXR is encoded to sRGB and alpha is unassociated", [&] {
            auto image = io::load(ctx, fixture("linear.exr"));
            const auto actual = test::read(ctx, image);
            const std::array<std::uint8_t, 8> expected{137, 188, 225, 255, 137, 99, 0, 128};
            test::check(actual.size() == expected.size(), "wrong EXR dimensions");
            for (std::size_t i = 0; i < actual.size(); ++i) {
                test::near(actual[i], expected[i], 1);
            }
            ctx.destroy(image);
        });
        test::run("all EXIF orientations rotate and mirror pixels", [&] {
            constexpr std::array<std::array<int, 6>, 7> orders{{{1, 0, 3, 2, 5, 4},
                                                                {5, 4, 3, 2, 1, 0},
                                                                {4, 5, 2, 3, 0, 1},
                                                                {0, 2, 4, 1, 3, 5},
                                                                {4, 2, 0, 5, 3, 1},
                                                                {5, 3, 1, 4, 2, 0},
                                                                {1, 3, 5, 0, 2, 4}}};
            constexpr std::array<std::array<std::uint8_t, 4>, 6> colors{{{255, 0, 0, 255},
                                                                         {0, 255, 0, 255},
                                                                         {0, 0, 255, 255},
                                                                         {255, 255, 0, 255},
                                                                         {0, 255, 255, 255},
                                                                         {255, 0, 255, 255}}};
            for (std::size_t n = 2; n <= 8; ++n) {
                auto image = io::load(ctx, fixture("orientation-" + std::to_string(n) + ".png"));
                test::check(image.size().width == (n >= 5 ? 3u : 2u) &&
                                image.size().height == (n >= 5 ? 2u : 3u),
                            "orientation dimensions incorrect");
                const auto actual = test::read(ctx, image);
                for (std::size_t i = 0; i < actual.size(); ++i) {
                    test::check(actual[i] == colors[std::size_t(orders[n - 2][i / 4])][i % 4],
                                "orientation pixels incorrect");
                }
                ctx.destroy(image);
            }
        });
        test::run("invalid paths and unsupported output have structured errors", [&] {
            auto image = io::load(ctx, fixture("rgb.png"));
            for (const auto& path : {std::string{}, std::string("bad\0.png", 8)}) {
                test::error(ErrorCode::invalid_argument, "io.load", "path",
                            [&] { (void)io::load(ctx, path); });
                test::error(ErrorCode::invalid_argument, "io.save", "path",
                            [&] { io::save(ctx, image, path); });
            }
            test::error(ErrorCode::invalid_argument, "io.save", "path",
                        [&] { io::save(ctx, image, files.file("unsupported.jpg")); });
            test::check(!fs::exists(files.file("unsupported.jpg")), "invalid save created a file");
            ctx.destroy(image);
        });
        test::run("local paths are literal including brackets percent and Unicode", [&] {
            const auto path = files.file("imágen[0]%03d.png");
            fs::copy_file(fixture("rgb.png"), path);
            auto image = io::load(ctx, path);
            io::save(ctx, image, path);
            auto loaded = io::load(ctx, path);
            pixels(ctx, loaded, {255, 0, 0, 255, 0, 255, 0, 255});
            ctx.destroy(image);
            ctx.destroy(loaded);
        });
        test::run("file and ICC failures leave the context reusable", [&] {
            std::ofstream(files.file("corrupt.png")) << "not an image";
            for (int i = 0; i < 3; ++i) {
                for (const auto& path : {files.file("missing.png"), files.file("corrupt.png"),
                                         fixture("invalid-icc.png")}) {
                    test::error(ErrorCode::io_failed, "io.load", "path",
                                [&] { (void)io::load(ctx, path); });
                }
                auto image = io::load(ctx, fixture("rgb.png"));
                test::error(ErrorCode::io_failed, "io.save", "path",
                            [&] { io::save(ctx, image, files.file("missing-dir/output.png")); });
                pixels(ctx, image, {255, 0, 0, 255, 0, 255, 0, 255});
                ctx.destroy(image);
            }
        });
        test::run("replaced files are read fresh", [&] {
            const auto path = files.file("replaced.png");
            fs::copy_file(fixture("rgb.png"), path);
            auto first = io::load(ctx, path);
            fs::copy_file(fixture("gray.png"), path, fs::copy_options::overwrite_existing);
            auto second = io::load(ctx, path);
            test::check(second.size().width == 3, "stale dimensions");
            pixels(ctx, first, {255, 0, 0, 255, 0, 255, 0, 255});
            pixels(ctx, second, {0, 0, 0, 255, 128, 128, 128, 255, 255, 255, 255, 255});
            ctx.destroy(first);
            ctx.destroy(second);
        });
        test::run("core resource errors keep their metadata", [&] {
            auto other = Context::create();
            auto foreign = other.create_image({1, 1});
            auto destroyed = ctx.create_image({1, 1});
            ctx.destroy(destroyed);
            for (const auto& image : {Image{}, foreign, destroyed}) {
                test::error(ErrorCode::invalid_resource, "create_readback_buffer", "image",
                            [&] { io::save(ctx, image, files.file("invalid.png")); });
            }
            test::check(!fs::exists(files.file("invalid.png")), "invalid resource created a file");
            other.destroy(foreign);
        });
        test::run("save waits for earlier queued processing", [&] {
            auto image = io::load(ctx, fixture("rgb.png"));
            auto cmd = ctx.create_commands();
            cmd.fill(image, {.color = {0, 0, 1, 1}});
            auto pending = ctx.submit(cmd);
            io::save(ctx, image, files.file("pending.png"));
            ctx.wait(pending);
            auto output = io::load(ctx, files.file("pending.png"));
            pixels(ctx, output, {0, 0, 255, 255, 0, 0, 255, 255});
            ctx.destroy(output);
            ctx.destroy(image);
        });
    });
    return test::finish();
}
