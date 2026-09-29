#include "../test.h"
#include <bit>
#include <filesystem>
#include <fstream>
#include <wgpupixel_io.h>

#ifdef WGPUPIXEL_TEST_EXIF_FAILURE
extern "C" {
#include <libavcodec/exif.h>
#include <libavutil/error.h>
int __real_av_exif_parse_buffer(void*, const uint8_t*, size_t, AVExifMetadata*, AVExifHeaderMode);
void __real_av_exif_free(AVExifMetadata*);
}
namespace {
bool fail_exif = false;
AVExifMetadata* failed_metadata = nullptr;
bool freed_metadata = false;
} // namespace
extern "C" int __wrap_av_exif_parse_buffer(void* log, const uint8_t* data, size_t size,
                                           AVExifMetadata* metadata, AVExifHeaderMode mode) {
    const int result = __real_av_exif_parse_buffer(log, data, size, metadata, mode);
    // Fail only the loader's optional reparse, after allocating metadata. FFmpeg
    // may already have decoded usable pixels when EXIF parsing fails.
    if (!log && fail_exif) {
        failed_metadata = metadata;
        return AVERROR_INVALIDDATA;
    }
    return result;
}
extern "C" void __wrap_av_exif_free(AVExifMetadata* metadata) {
    if (metadata == failed_metadata) {
        freed_metadata = true;
        failed_metadata = nullptr;
    }
    __real_av_exif_free(metadata);
}
#endif

using namespace wgpupixel;
namespace {
std::string fixture(std::string_view name) {
    return (std::filesystem::path(WGPUPIXEL_TEST_DATA) / name).string();
}

std::vector<float> read(Context& ctx, std::string_view name) {
    auto image = io::load(ctx, fixture(name));
    test::check(image.size().width == 3 && image.size().height == 2, "wrong dimensions");
    auto result = test::read_float(ctx, image);
    ctx.destroy(image);
    return result;
}

void wide(Context& ctx, std::string_view name, std::string_view expected_name) {
    const auto actual = read(ctx, name);
    std::ifstream expected(fixture(expected_name));
    for (float sample : actual) {
        double value = 0;
        test::check(bool(expected >> value), "missing independent matrix/TRC expectation");
        test::near(sample, static_cast<float>(value), 2e-6f * std::max(1.0f, std::abs(float(value))));
    }
}

void rejected(Context& ctx, std::string_view name, std::string_view diagnostic) {
    test::error(ErrorCode::io_failed, "io.load", "path", [&] {
        try {
            auto image = io::load(ctx, fixture(name));
            ctx.destroy(image);
        } catch (const Error& error) {
            test::check(std::string_view(error.what()).find(diagnostic) != std::string_view::npos,
                        "missing actionable ICC diagnostic");
            throw;
        }
    });
}

void eight_bit(Context& ctx, std::string_view name, bool gray, bool icc = false) {
    constexpr std::array<std::array<int, 4>, 6> samples{{{1, 10, 11, 255},
                                                         {80, 160, 240, 128},
                                                         {255, 80, 40, 1},
                                                         {255, 16, 240, 0},
                                                         {128, 129, 130, 255},
                                                         {17, 200, 250, 192}}};
    const auto actual = read(ctx, name);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double alpha = samples[i][3] / 255.0;
        test::near(actual[4 * i + 3], static_cast<float>(alpha), 1e-7f);
        for (std::size_t c = 0; c < 3; ++c) {
            double value = samples[i][gray ? 0 : c] / 255.0;
            if (!gray) {
                value = alpha ? std::round(samples[i][c] * alpha) / 255 / alpha : 0;
            }
            const double linear =
                value <= 0.04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
            test::near(actual[4 * i + c], static_cast<float>(linear * alpha),
                       icc ? 4.0f / 65536 : 2e-6f);
        }
    }
}
} // namespace

int main() {
    test::run("ICC precision regressions", [] {
        auto ctx = Context::create();
        for (auto name : {"precision-wide-icc.png", "precision-wide-icc.tiff"}) {
            test::run(name, [&] { wide(ctx, name, "precision-wide-expected.txt"); });
        }
        test::run("wide-gamut associated 16-bit ICC", [&] {
            wide(ctx, "precision-wide-associated-icc.tiff",
                 "precision-wide-associated-expected.txt");
        });
        test::run("16-bit ICC retains out-of-sRGB-gamut negative channels", [&] {
            const auto actual = read(ctx, "precision-wide-icc.png");
            test::near(actual[12], -0.398333826f, 2e-6f);
            test::near(actual[14], -0.042937964f, 2e-6f);
        });
        test::run("float parametric ICC preserves HDR with documented negative extension", [&] {
            wide(ctx, "precision-float-parametric-icc.tiff", "precision-float-parametric-expected.txt");
        });
        test::run("threaded ICC conversion matches independent matrix at every pixel", [&] {
            auto image = io::load(ctx, fixture("precision-wide-threaded-icc.png"));
            test::check(image.size().width == 1024 && image.size().height == 1025,
                        "wrong dimensions");
            const auto actual = test::read_float(ctx, image);
            std::array<double, 24> expected{};
            std::ifstream file(fixture("precision-wide-expected.txt"));
            for (auto& value : expected) {
                test::check(bool(file >> value), "missing matrix expectation");
            }
            for (std::size_t i = 0; i < actual.size(); ++i) {
                test::near(actual[i], static_cast<float>(expected[i % expected.size()]), 2e-6f);
            }
            ctx.destroy(image);
        });
        for (auto name : {"precision-float-table-icc.tiff", "precision-float-lut-icc.tiff",
                          "precision-float-hybrid-icc.tiff"}) {
            test::run(name, [&] { rejected(ctx, name, "matrix/shaper"); });
        }
        for (auto name : {"precision-table-icc.png", "precision-lut-icc.png"}) {
            test::run(name, [&] { (void)read(ctx, name); });
        }
        for (auto name : {"precision-float-huge-icc.tiff", "precision-float-overflow-icc.tiff"}) {
            test::run(name, [&] { rejected(ctx, name, "sample"); });
        }
        test::run("non-finite ICC input",
                  [&] { rejected(ctx, "precision-float-nonfinite-icc.tiff", "non-finite"); });
        test::run("linear associated ICC keeps color at zero alpha", [&] {
            const auto actual = read(ctx, "precision-float-associated-icc.tiff");
            const auto expected = read(ctx, "precision-float-associated.tiff");
            for (std::size_t i = 0; i < actual.size(); ++i) {
                const auto p = i / 4 * 4;
                const float scale =
                    std::max({1.0f, std::abs(expected[p]), std::abs(expected[p + 1]),
                              std::abs(expected[p + 2])});
                test::near(actual[i], expected[i], 4.0f / 65536 * scale);
            }
        });
        test::run("linear associated ICC matrix does not round-trip alpha", [&] {
            const auto actual = read(ctx, "precision-float-alpha-independent-icc.tiff");
            for (std::size_t i = 1; i < 6; ++i) {
                for (std::size_t c = 0; c < 3; ++c) {
                    test::near(actual[4 * i + c], actual[c], 0);
                }
            }
        });
        test::run("every orientation preserves float bits and zero-alpha color", [&] {
            constexpr std::array<std::array<int, 6>, 7> orders{{{2, 1, 0, 5, 4, 3},
                                                                {5, 4, 3, 2, 1, 0},
                                                                {3, 4, 5, 0, 1, 2},
                                                                {0, 3, 1, 4, 2, 5},
                                                                {3, 0, 4, 1, 5, 2},
                                                                {5, 2, 4, 1, 3, 0},
                                                                {2, 5, 1, 4, 0, 3}}};
            const auto original = read(ctx, "precision-float-associated.tiff");
            for (unsigned direction = 2; direction <= 8; ++direction) {
                const auto name = direction == 6 ? "precision-float-oriented.tiff"
                                                 : "precision-float-orientation-" +
                                                       std::to_string(direction) + ".tiff";
                auto image = io::load(ctx, fixture(name));
                test::check(image.size().width == (direction >= 5 ? 2u : 3u) &&
                                image.size().height == (direction >= 5 ? 3u : 2u),
                            "wrong orientation dimensions");
                const auto actual = test::read_float(ctx, image);
                for (std::size_t i = 0; i < actual.size(); ++i) {
                    const float expected = original[4 * orders[direction - 2][i / 4] + i % 4];
                    test::check(std::bit_cast<std::uint32_t>(actual[i]) ==
                                    std::bit_cast<std::uint32_t>(expected),
                                "orientation changed float bits");
                }
                ctx.destroy(image);
            }
        });
        test::run("8-bit gray TIFF uses sRGB",
                  [&] { eight_bit(ctx, "precision-8-gray.tiff", true); });
        test::run("8-bit associated TIFF",
                  [&] { eight_bit(ctx, "precision-8-associated.tiff", false); });
        test::run("8-bit associated ICC TIFF",
                  [&] { eight_bit(ctx, "precision-8-associated-icc.tiff", false, true); });
#ifdef WGPUPIXEL_TEST_EXIF_FAILURE
        test::run("optional TIFF EXIF parse failure frees metadata and keeps pixels", [&] {
            const auto expected = read(ctx, "precision.tiff");
            struct Reset {
                ~Reset() {
                    fail_exif = false;
                }
            } reset;
            fail_exif = true;
            const auto actual = read(ctx, "precision.tiff");
            test::check(actual == expected, "optional EXIF failure changed decoded pixels");
            test::check(freed_metadata, "failed EXIF parse leaked metadata");
        });
#endif
        test::run("context remains usable after ICC failures",
                  [&] { (void)read(ctx, "precision.tiff"); });
    });
    return test::finish();
}
