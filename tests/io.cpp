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
constexpr std::array<std::array<std::uint16_t, 4>, 6> integer_samples{{{1, 2650, 2651, 65535},
                                                                       {12345, 32767, 54321, 40001},
                                                                       {65534, 32768, 257, 1},
                                                                       {65535, 4096, 61680, 0},
                                                                       {32768, 32769, 32770, 65535},
                                                                       {17, 40000, 60000, 32769}}};
constexpr std::array<std::array<float, 4>, 6> float_samples{{{2.5f, -0.125f, 1 + 0x1p-23f, 1},
                                                             {0.123456789f, 0.03125f, 4.5f, 0.5f},
                                                             {0.25f, -0.5f, 8, 0},
                                                             {65504, -12.5f, 1.0f / 3, 1},
                                                             {0x1p-14f, 0x1p-24f, -0.0f, 0.25f},
                                                             {1.125f, 1.126f, 0.1f, 0.75f}}};
// The same values rounded once to binary16 by Python's struct.pack('<e').
constexpr std::array<std::array<float, 4>, 6> half_samples{
    {{2.5f, -0.125f, 1, 1},
     {0.12347412109375f, 0.03125f, 4.5f, 0.5f},
     {0.25f, -0.5f, 8, 0},
     {65504, -12.5f, 0.333251953125f, 1},
     {0x1p-14f, 0x1p-24f, -0.0f, 0.25f},
     {1.125f, 1.1259765625f, 0.0999755859375f, 0.75f}}};
constexpr std::array<std::size_t, 6> rotated{3, 0, 4, 1, 5, 2};

float linear(double value) {
    return static_cast<float>(value <= 0.04045 ? value / 12.92
                                               : std::pow((value + 0.055) / 1.055, 2.4));
}

struct IntegerOptions {
    bool gray = false;
    double gamma = 0;
    bool associated = false;
    bool oriented = false;
    float tolerance = 2e-6f;
};

void integer_precision(Context& ctx, std::string_view name, IntegerOptions options = {}) {
    auto image = io::load(ctx, fixture(name));
    test::check(image.size().width == (options.oriented ? 2u : 3u) &&
                    image.size().height == (options.oriented ? 3u : 2u),
                "wrong precision dimensions");
    const auto actual = test::read_float(ctx, image);
    for (std::size_t i = 0; i < integer_samples.size(); ++i) {
        const auto& source = integer_samples[options.oriented ? rotated[i] : i];
        const double alpha = source[3] / 65535.0;
        test::near(actual[i * 4 + 3], static_cast<float>(alpha), 1e-7f);
        for (std::size_t c = 0; c < 3; ++c) {
            double encoded = source[options.gray ? 0 : c] / 65535.0;
            if (options.associated) {
                encoded = std::round(source[options.gray ? 0 : c] * alpha) / 65535;
                encoded = alpha > 0 ? encoded / alpha : 0;
            }
            const double decoded =
                options.gamma ? std::pow(encoded, options.gamma) : linear(encoded);
            const float expected = static_cast<float>(decoded * alpha);
            test::near(actual[i * 4 + c], expected,
                       options.tolerance * std::max(1.0f, std::abs(expected)));
        }
    }
    ctx.destroy(image);
}

struct FloatOptions {
    bool half = false;
    bool straight = false;
    bool gray = false;
    bool alpha = true;
    bool oriented = false;
    float tolerance = 0;
};

void float_precision(Context& ctx, std::string_view name, FloatOptions options = {}) {
    auto image = io::load(ctx, fixture(name));
    test::check(image.size().width == (options.oriented ? 2u : 3u) &&
                    image.size().height == (options.oriented ? 3u : 2u),
                "wrong float dimensions");
    const auto actual = test::read_float(ctx, image);
    for (std::size_t i = 0; i < float_samples.size(); ++i) {
        const auto& source =
            (options.half ? half_samples : float_samples)[options.oriented ? rotated[i] : i];
        const float opacity = options.alpha ? source[3] : 1;
        test::near(actual[i * 4 + 3], opacity, 0);
        const float scale =
            std::max({1.0f, std::abs(source[0]), std::abs(source[1]), std::abs(source[2])});
        for (std::size_t c = 0; c < 3; ++c) {
            const float expected = source[options.gray ? 0 : c] * (options.straight ? opacity : 1);
            test::near(actual[i * 4 + c], expected, options.tolerance * scale);
            if (expected == 0 && options.tolerance == 0) {
                test::check(std::signbit(actual[i * 4 + c]) == std::signbit(expected),
                            "lost signed zero");
            }
        }
    }
    ctx.destroy(image);
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
            const auto wide_samples = test::read_float(ctx, wide);
            const std::array<float, 8> expected{1,
                                                0,
                                                0,
                                                1,
                                                linear(32768.0 / 65535),
                                                linear(32768.0 / 65535),
                                                linear(32768.0 / 65535),
                                                1};
            for (std::size_t i = 0; i < expected.size(); ++i) {
                test::near(wide_samples[i], expected[i], 2e-6f);
            }
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
            const auto wide_samples = test::read_float(ctx, wide);
            const float alpha = 32768.0f / 65535;
            const std::array<float, 4> expected{linear(32768.0 / 65535) * alpha,
                                                linear(16384.0 / 65535) * alpha, alpha, alpha};
            for (std::size_t i = 0; i < expected.size(); ++i) {
                test::near(wide_samples[i], expected[i], 3.0f / 65535);
            }
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
        test::run("linear EXR preserves premultiplied samples", [&] {
            auto image = io::load(ctx, fixture("linear.exr"));
            const auto actual = test::read_float(ctx, image);
            const std::array<float, 8> expected{0.25f, 0.5f, 0.75f, 1, 0.125f, 0.0625f, 0, 0.5f};
            test::check(actual.size() == expected.size(), "wrong EXR dimensions");
            for (std::size_t i = 0; i < actual.size(); ++i) {
                test::near(actual[i], expected[i], 0);
            }
            ctx.destroy(image);
        });
        test::run("16-bit PNG and TIFF retain low bits and use the sRGB EOTF", [&] {
            for (const auto* name :
                 {"precision.png", "precision.tiff", "precision-planar-be.tiff"}) {
                integer_precision(ctx, name);
            }
            integer_precision(ctx, "precision-gray.png", {.gray = true});
            integer_precision(ctx, "precision-gray.tiff", {.gray = true});
            integer_precision(ctx, "precision-oriented.png", {.oriented = true});
            integer_precision(ctx, "precision-associated.tiff", {.associated = true});
        });
        test::run("16-bit ICC conversion retains precision and alpha", [&] {
            // One 16-bit output code step times the maximum sRGB EOTF slope (< 2.3).
            for (const auto* name : {"precision-icc.png", "precision-icc.tiff"}) {
                integer_precision(ctx, name, {.tolerance = 3.0f / 65535});
            }
            integer_precision(ctx, "precision-gray-icc.png",
                              {.gray = true, .gamma = 2.2, .tolerance = 3.0f / 65535});
            integer_precision(ctx, "precision-associated-icc.tiff",
                              {.associated = true, .tolerance = 3.0f / 65535});
        });
        test::run("tagged linear and gamma PNGs retain precision", [&] {
            integer_precision(ctx, "precision-linear.png", {.gamma = 1});
            integer_precision(ctx, "precision-gamma22.png", {.gamma = 2.2});
            integer_precision(ctx, "precision-gamma28.png", {.gamma = 2.8});
        });
        test::run("half and float EXR preserve HDR negatives and zero-alpha emission exactly", [&] {
            float_precision(ctx, "precision-float.exr");
            float_precision(ctx, "precision-half.exr", {.half = true});
            float_precision(ctx, "precision-rgb.exr", {.alpha = false});
            float_precision(ctx, "precision-gray.exr", {.gray = true});
            float_precision(ctx, "precision-gray-half.exr",
                            {.half = true, .gray = true, .alpha = false});
        });
        test::run("float TIFF preserves HDR precision and respects alpha association", [&] {
            float_precision(ctx, "precision-float.tiff", {.straight = true});
            float_precision(ctx, "precision-float-be.tiff", {.straight = true});
            float_precision(ctx, "precision-float-rgb.tiff", {.alpha = false});
            float_precision(ctx, "precision-float-associated.tiff");
            float_precision(ctx, "precision-float-planar-be.tiff");
            float_precision(ctx, "precision-float-oriented.tiff", {.oriented = true});
            // ICC colorants are serialized as s15Fixed16; bound matrix rounding by input magnitude.
            float_precision(ctx, "precision-float-icc.tiff",
                            {.straight = true, .tolerance = 4.0f / 65536});
        });
        test::run("non-finite half and float input is rejected", [&] {
            for (const auto* name : {"precision-invalid.exr", "precision-invalid-half.exr"}) {
                test::error(ErrorCode::io_failed, "io.load", "path",
                            [&] { (void)io::load(ctx, fixture(name)); });
            }
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
