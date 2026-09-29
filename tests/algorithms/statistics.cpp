#include "analysis.h"
#include <array>
#include <limits>

using namespace wgpupixel;

namespace {
struct Reference {
    std::array<double, 5> count{}, sum{}, squares{}, minimum{}, maximum{};
    std::array<double, 4> premultiplied{};
};

Reference reference(std::span<const float> pixels, std::int64_t width, ColorEncoding space,
                    std::span<const std::uint8_t> mask = {},
                    const std::optional<Rect>& region = {}) {
    Reference result;
    result.minimum.fill(std::numeric_limits<double>::infinity());
    result.maximum.fill(-std::numeric_limits<double>::infinity());
    const auto height = std::int64_t(pixels.size() / 4) / width;
    for (int pass = 0; pass < 2; ++pass) {
        for (std::int64_t y = 0; y < height; ++y) {
            for (std::int64_t x = 0; x < width; ++x) {
                if (!analysis::selected(x, y, width, mask, region)) {
                    continue;
                }
                const auto* pixel = &pixels[(y * width + x) * 4];
                const auto measured = analysis::measure(pixel, space);
                for (std::size_t c = 0; c < 5; ++c) {
                    if (c != 3 && !measured.color) {
                        continue;
                    }
                    const double value = measured.values[c];
                    if (pass == 0) {
                        ++result.count[c];
                        result.sum[c] += value;
                        result.minimum[c] = std::min(result.minimum[c], value);
                        result.maximum[c] = std::max(result.maximum[c], value);
                    } else {
                        const double mean = result.sum[c] / result.count[c];
                        result.squares[c] += (value - mean) * (value - mean);
                    }
                }
                if (pass == 0) {
                    for (std::size_t c = 0; c < 4; ++c) {
                        result.premultiplied[c] += pixel[c];
                    }
                }
            }
        }
    }
    return result;
}

void expect(const ImageStatistics& actual, const Reference& expected, double tolerance = 2e-5) {
    test::check(actual.pixels == std::uint64_t(expected.count[3]), "measured pixel count differs");
    test::check(actual.color_pixels == std::uint64_t(expected.count[0]),
                "color pixel count differs");
    const std::array channels{actual.red, actual.green, actual.blue, actual.alpha,
                              actual.luminosity};
    for (std::size_t c = 0; c < 5; ++c) {
        const auto& channel = channels[c];
        const double count = expected.count[c];
        if (count == 0) {
            test::check(channel.minimum == 0 && channel.maximum == 0 && channel.mean == 0 &&
                            channel.deviation == 0,
                        "an empty channel reports zeros");
            continue;
        }
        const auto close = [&](double a, double b) {
            test::check(std::abs(a - b) <= tolerance * std::max(1.0, std::abs(b)),
                        "channel " + std::to_string(c) + ": expected " + std::to_string(b) +
                            ", got " + std::to_string(a));
        };
        close(channel.minimum, expected.minimum[c]);
        close(channel.maximum, expected.maximum[c]);
        close(channel.mean, expected.sum[c] / count);
        close(channel.deviation, std::sqrt(expected.squares[c] / count));
    }
    const std::array average{actual.average.r, actual.average.g, actual.average.b,
                             actual.average.a};
    for (std::size_t c = 0; c < 4; ++c) {
        const double mean = expected.count[3] ? expected.premultiplied[c] / expected.count[3] : 0;
        test::near(average[c], float(mean), 1e-5f);
    }
}

ImageStatistics measure(Context& ctx, const Image& image, const StatisticsBuffer& buffer,
                        const AnalysisOptions& options) {
    auto cmd = ctx.create_commands(1);
    cmd.statistics(image, buffer, options);
    ctx.submit_and_wait(cmd);
    return ctx.read(buffer);
}
} // namespace

int main() {
    test::run("statistics match double-precision references for spaces, masks and regions", [] {
        auto ctx = Context::create();
        constexpr std::int64_t width = 61, height = 29;
        auto image = ctx.create_image({width, height});
        auto mask = ctx.create_mask({width, height});
        auto buffer = ctx.create_statistics_buffer();
        const auto pixels = analysis::pixels(width * height, 17);
        const auto coverage = analysis::coverage(width * height, 19);
        analysis::upload(ctx, image, pixels);
        analysis::upload(ctx, mask, coverage);
        for (auto space : {ColorEncoding::srgb, ColorEncoding::linear}) {
            for (bool masked : {false, true}) {
                for (auto region : {std::optional<Rect>{}, std::optional<Rect>{Rect{7, -3, 40, 20}},
                                    std::optional<Rect>{Rect{12, 5, 1, 1}}}) {
                    const AnalysisOptions options{
                        .space = space, .mask = masked ? &mask : nullptr, .region = region};
                    expect(measure(ctx, image, buffer, options),
                           reference(pixels, width, space,
                                     masked ? std::span(coverage) : std::span<const std::uint8_t>{},
                                     region));
                }
            }
        }
        ctx.destroy(buffer);
        ctx.destroy(mask);
        ctx.destroy(image);
    });

    test::run("statistics stay accurate for large images with a large mean", [] {
        auto ctx = Context::create();
        constexpr std::int64_t width = 1024, height = 1031;
        auto image = ctx.create_image({width, height});
        auto buffer = ctx.create_statistics_buffer();
        std::mt19937 random(23);
        std::uniform_real_distribution<float> noise(0.0f, 0.01f);
        std::vector<float> pixels(width * height * 4);
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            // Linear HDR values near 100: naive float sums of squares would cancel.
            pixels[i] = 100 + noise(random);
            pixels[i + 1] = noise(random);
            pixels[i + 2] = 0.5f;
            pixels[i + 3] = 1;
        }
        analysis::upload(ctx, image, pixels);
        const auto actual = measure(ctx, image, buffer, {.space = ColorEncoding::linear});
        const auto expected = reference(pixels, width, ColorEncoding::linear);
        test::check(actual.pixels == std::uint64_t(width * height), "pixel count differs");
        test::near(float(actual.red.mean), float(expected.sum[0] / expected.count[0]), 2e-5f);
        const double deviation = std::sqrt(expected.squares[0] / expected.count[0]);
        test::check(std::abs(actual.red.deviation - deviation) <= 1e-3 * deviation,
                    "deviation around a large mean lost precision");
        test::near(float(actual.green.deviation),
                   float(std::sqrt(expected.squares[1] / expected.count[1])), 1e-6f);
        test::check(actual.blue.deviation == 0 && actual.blue.minimum == 0.5 &&
                        actual.blue.maximum == 0.5,
                    "a constant channel has no deviation");
        ctx.destroy(buffer);
        ctx.destroy(image);
    });

    test::run("eyedropper sample regions average premultiplied color", [] {
        const auto point = sample_region({10.7f, 3.2f});
        test::check(point.x == 10 && point.y == 3 && point.width == 1 && point.height == 1,
                    "point sample covers the pixel under the point");
        const auto average = sample_region({10.7f, 3.2f}, 5);
        test::check(average.x == 8 && average.y == 1 && average.width == 5 && average.height == 5,
                    "5 by 5 average is centered on that pixel");
        const auto negative = sample_region({-0.5f, 0}, 3);
        test::check(negative.x == -2 && negative.y == -1, "negative coordinates floor");
        test::error(ErrorCode::invalid_argument, "sample_region", "size",
                    [] { (void)sample_region({0, 0}, 4); });
        test::error(ErrorCode::invalid_argument, "sample_region", "size",
                    [] { (void)sample_region({0, 0}, 0); });
        test::error(ErrorCode::invalid_argument, "sample_region", "point",
                    [] { (void)sample_region({std::numeric_limits<float>::quiet_NaN(), 0}, 3); });
        test::error(ErrorCode::invalid_argument, "sample_region", "point",
                    [] { (void)sample_region({3e9f, 0}, 3); });

        auto ctx = Context::create();
        constexpr std::int64_t width = 9, height = 7;
        auto image = ctx.create_image({width, height});
        auto buffer = ctx.create_statistics_buffer();
        const auto pixels = analysis::pixels(width * height, 29);
        analysis::upload(ctx, image, pixels);
        // A 5 by 5 sample at the corner averages only the 3 x 3 pixels inside the image.
        const auto corner = sample_region({0.25f, 0.75f}, 5);
        const auto sampled = measure(ctx, image, buffer, {.region = corner});
        test::check(sampled.pixels == 9, "samples outside the image are ignored");
        expect(sampled, reference(pixels, width, ColorEncoding::srgb, {}, corner));
        ctx.destroy(buffer);
        ctx.destroy(image);
    });

    test::run("statistics contracts, empty selections and buffer reuse", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({4, 4});
        auto mask = ctx.create_mask({4, 4});
        auto buffer = ctx.create_statistics_buffer();
        test::error(ErrorCode::invalid_argument, "read", "buffer", [&] { (void)ctx.read(buffer); });
        auto cmd = ctx.create_commands(4);
        cmd.fill(image, {.color = {0.5f, 0.25f, 0.125f, 0.5f}});
        cmd.fill(mask, {.coverage = 0.498f}); // Byte 127: less than half selected.
        cmd.statistics(image, buffer, {.mask = &mask});
        test::error(ErrorCode::resource_busy, "read", "buffer", [&] { (void)ctx.read(buffer); });
        ctx.submit_and_wait(cmd);
        const auto empty = ctx.read(buffer);
        test::check(empty.pixels == 0 && empty.color_pixels == 0 && empty.alpha.maximum == 0 &&
                        empty.average.a == 0,
                    "an empty selection reports no pixels");
        cmd.fill(mask, {.coverage = 0.5f}); // Byte 128: measured.
        cmd.statistics(image, buffer, {.mask = &mask, .region = Rect{1, 1, 2, 2}});
        ctx.submit_and_wait(cmd);
        const auto half = ctx.read(buffer);
        test::check(half.pixels == 4 && half.color_pixels == 4, "half-selected pixels count");
        test::near(float(half.red.mean), float(analysis::encode(1.0)), 1e-6f);
        test::near(float(half.alpha.mean), 0.5f, 1e-7f);
        test::near(half.average.r, 0.5f, 1e-7f);
        test::near(half.average.a, 0.5f, 1e-7f);
        test::error(ErrorCode::invalid_argument, "statistics", "region",
                    [&] { cmd.statistics(image, buffer, {.region = Rect{0, 0, 1, -1}}); });
        test::error(ErrorCode::invalid_argument, "statistics", "space", [&] {
            cmd.statistics(image, buffer, {.space = static_cast<ColorEncoding>(9)});
        });
        test::error(ErrorCode::invalid_resource, "statistics", "destination",
                    [&] { cmd.statistics(image, StatisticsBuffer{}, {}); });
        test::error(ErrorCode::invalid_resource, "statistics", "source",
                    [&] { cmd.statistics(Image{}, buffer, {}); });
        ctx.destroy(buffer);
        test::error(ErrorCode::invalid_resource, "read", "buffer", [&] { (void)ctx.read(buffer); });
        ctx.destroy(mask);
        ctx.destroy(image);
    });
    test::run("statistics keep huge but finite HDR deviations", [] {
        auto ctx = Context::create();
        auto buffer = ctx.create_statistics_buffer();
        // Closed form: values +v and -v in equal numbers have mean 0 and deviation v. Their
        // squares overflow float32 unless the moments are scaled.
        for (auto [size, value] :
             {std::pair{ImageSize{2, 1}, 1e20f}, std::pair{ImageSize{512, 300}, 1e20f},
              std::pair{ImageSize{256, 256}, 3e38f}}) {
            auto image = ctx.create_image(size);
            std::vector<float> pixels;
            for (std::int64_t i = 0; i < size.width * size.height; ++i) {
                pixels.insert(pixels.end(), {i % 2 ? -value : value, 0.5f, 0, 1});
            }
            analysis::upload(ctx, image, pixels);
            auto cmd = ctx.create_commands(1);
            cmd.statistics(image, buffer, {.space = ColorEncoding::linear});
            ctx.submit_and_wait(cmd);
            const auto result = ctx.read(buffer);
            test::check(std::abs(result.red.mean) <= 1e-6 * value,
                        "HDR mean must cancel: " + std::to_string(result.red.mean));
            test::check(std::abs(result.red.deviation / value - 1) <= 1e-6,
                        "HDR deviation: expected " + std::to_string(value) + ", got " +
                            std::to_string(result.red.deviation));
            test::check(result.red.minimum == -value && result.red.maximum == value,
                        "HDR extremes differ");
            test::check(result.green.deviation == 0 && result.green.mean == 0.5,
                        "small channels keep full precision");
            ctx.destroy(image);
        }
        ctx.destroy(buffer);
    });
    return test::finish();
}
