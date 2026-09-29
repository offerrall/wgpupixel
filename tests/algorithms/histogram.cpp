#include "analysis.h"
#include <array>
#include <limits>

using namespace wgpupixel;

namespace {
constexpr std::int64_t width = 53, height = 37;

std::vector<std::uint32_t> reference(std::span<const float> pixels, std::uint32_t bins,
                                     ColorEncoding space, std::span<const std::uint8_t> mask = {},
                                     const std::optional<Rect>& region = {}) {
    std::vector<std::uint32_t> counts(bins * analysis_channel_count);
    for (std::int64_t y = 0; y < height; ++y) {
        for (std::int64_t x = 0; x < width; ++x) {
            if (!analysis::selected(x, y, width, mask, region)) {
                continue;
            }
            const auto measured = analysis::measure(&pixels[(y * width + x) * 4], space);
            for (std::uint32_t c = 0; c < analysis_channel_count; ++c) {
                if (c == 3 || measured.color) {
                    ++counts[c * bins + analysis::bin(measured.values[c], bins)];
                }
            }
        }
    }
    return counts;
}

std::vector<std::uint32_t> measure(Context& ctx, const Image& image, const HistogramBuffer& buffer,
                                   const AnalysisOptions& options) {
    auto cmd = ctx.create_commands(1);
    cmd.histogram(image, buffer, options);
    ctx.submit_and_wait(cmd);
    std::vector<std::uint32_t> counts(buffer.bins() * analysis_channel_count);
    ctx.read(buffer, counts);
    return counts;
}
} // namespace

int main() {
    test::run(
        "histogram counts match the CPU exactly for every space, bin count, mask and region", [] {
            auto ctx = Context::create();
            auto image = ctx.create_image({width, height});
            auto mask = ctx.create_mask({width, height});
            const auto pixels = analysis::pixels(width * height, 7, {2, 256, 1000, 4096});
            const auto coverage = analysis::coverage(width * height, 11);
            analysis::upload(ctx, image, pixels);
            analysis::upload(ctx, mask, coverage);
            const std::optional<Rect> region = Rect{-4, 9, 31, 100};
            // 1000 and 4096 bins exceed one workgroup's local bins and take extra layers.
            for (std::uint32_t bins : {2u, 256u, 1000u, 4096u}) {
                auto buffer = ctx.create_histogram_buffer(bins);
                test::check(buffer.bins() == bins, "histogram buffer reports its bins");
                for (auto space : {ColorEncoding::srgb, ColorEncoding::linear}) {
                    for (bool masked : {false, true}) {
                        for (bool clipped : {false, true}) {
                            const AnalysisOptions options{.space = space,
                                                          .mask = masked ? &mask : nullptr,
                                                          .region =
                                                              clipped ? region : std::nullopt};
                            const auto expected = reference(
                                pixels, bins, space,
                                masked ? std::span(coverage) : std::span<const std::uint8_t>{},
                                options.region);
                            test::check(measure(ctx, image, buffer, options) == expected,
                                        "histogram differs from the CPU reference");
                        }
                    }
                }
                ctx.destroy(buffer);
            }
        });

    test::run("256 sRGB bins equal the byte histogram of an RGBA8 download", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({width, height});
        const auto pixels = analysis::pixels(width * height, 3, {256});
        analysis::upload(ctx, image, pixels);
        auto buffer = ctx.create_histogram_buffer();
        const auto counts = measure(ctx, image, buffer, {});
        const auto bytes = test::read(ctx, image);
        std::vector<std::uint32_t> expected(256 * 4);
        for (std::size_t i = 0; i < bytes.size(); i += 4) {
            for (std::size_t c = 0; c < 4; ++c) {
                if (c == 3 || pixels[i + 3] > 0) {
                    ++expected[c * 256 + bytes[i + c]];
                }
            }
        }
        test::check(std::equal(expected.begin(), expected.end(), counts.begin()),
                    "8-bit levels differ from the downloaded bytes");
        std::uint64_t luminosity = 0, red = 0;
        for (std::size_t i = 0; i < 256; ++i) {
            luminosity += counts[4 * 256 + i];
            red += counts[i];
        }
        test::check(luminosity == red, "luminosity must count every pixel with color");
        ctx.destroy(buffer);
        ctx.destroy(image);
    });
    test::run("histogram spreads large and uniform images across workgroups without loss", [] {
        auto ctx = Context::create();
        constexpr std::int64_t w = 700, h = 500;
        auto image = ctx.create_image({w, h});
        auto buffer = ctx.create_histogram_buffer(4096);
        auto cmd = ctx.create_commands(2);
        // Every pixel contends for one bin per channel.
        cmd.fill(image, {.color = {0.25f, 0.5f, 0.125f, 0.5f}});
        cmd.histogram(image, buffer, {.space = ColorEncoding::linear});
        ctx.submit_and_wait(cmd);
        std::vector<std::uint32_t> counts(4096 * analysis_channel_count);
        ctx.read(buffer, counts);
        const std::array<std::uint32_t, 5> expected_bins{
            analysis::bin(0.5, 4096), analysis::bin(1.0, 4096), analysis::bin(0.25, 4096),
            analysis::bin(0.5, 4096), analysis::bin(0.2126 * 0.5 + 0.7152 + 0.0722 * 0.25, 4096)};
        for (std::uint32_t c = 0; c < analysis_channel_count; ++c) {
            test::check(counts[c * 4096 + expected_bins[c]] == w * h,
                        "uniform image must fill exactly one bin per channel");
        }
        // Random content over many workgroups, measured twice: counts replace, not accumulate.
        const auto pixels = analysis::pixels(w * h, 5, {4096});
        analysis::upload(ctx, image, pixels);
        for (int pass = 0; pass < 2; ++pass) {
            cmd.histogram(image, buffer, {});
            ctx.submit_and_wait(cmd);
            ctx.read(buffer, counts);
            std::vector<std::uint32_t> expected(counts.size());
            for (std::size_t i = 0; i < pixels.size(); i += 4) {
                const auto measured = analysis::measure(&pixels[i], ColorEncoding::srgb);
                for (std::uint32_t c = 0; c < analysis_channel_count; ++c) {
                    if (c == 3 || measured.color) {
                        ++expected[c * 4096 + analysis::bin(measured.values[c], 4096)];
                    }
                }
            }
            test::check(counts == expected, "large histogram differs from the CPU reference");
        }
        ctx.destroy(buffer);
        ctx.destroy(image);
    });

    test::run("histogram contracts: bins, spans, lifetime, empty regions and validation", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({4, 3});
        auto small = ctx.create_mask({3, 3});
        test::error(ErrorCode::invalid_argument, "create_histogram_buffer", "bins",
                    [&] { (void)ctx.create_histogram_buffer(1); });
        test::error(ErrorCode::invalid_argument, "create_histogram_buffer", "bins",
                    [&] { (void)ctx.create_histogram_buffer(4097); });
        auto buffer = ctx.create_histogram_buffer(16);
        std::vector<std::uint32_t> counts(16 * analysis_channel_count, 7);
        test::error(ErrorCode::invalid_argument, "read", "buffer",
                    [&] { ctx.read(buffer, counts); });
        auto cmd = ctx.create_commands(4);
        cmd.fill(image, {.color = {0, 0, 0, 0}});
        test::error(ErrorCode::invalid_argument, "histogram", "region",
                    [&] { cmd.histogram(image, buffer, {.region = Rect{0, 0, -1, 2}}); });
        test::error(ErrorCode::invalid_argument, "histogram", "space", [&] {
            cmd.histogram(image, buffer, {.space = static_cast<ColorEncoding>(2)});
        });
        test::error(ErrorCode::invalid_argument, "histogram", "mask",
                    [&] { cmd.histogram(image, buffer, {.mask = &small}); });
        test::error(ErrorCode::invalid_resource, "histogram", "destination",
                    [&] { cmd.histogram(image, HistogramBuffer{}, {}); });
        // Fully transparent pixels count only in alpha; an empty region counts nothing.
        cmd.histogram(image, buffer, {});
        test::error(ErrorCode::resource_busy, "read", "buffer", [&] { ctx.read(buffer, counts); });
        test::error(ErrorCode::resource_busy, "destroy", "resource", [&] { ctx.destroy(buffer); });
        ctx.submit_and_wait(cmd);
        ctx.read(buffer, counts);
        for (std::size_t i = 0; i < counts.size(); ++i) {
            test::check(counts[i] == (i == 3 * 16 ? 12u : 0u), "transparent pixels have no color");
        }
        cmd.histogram(image, buffer,
                      {.region = Rect{std::numeric_limits<std::int32_t>::max(), 0, 5, 5}});
        ctx.submit_and_wait(cmd);
        ctx.read(buffer, counts);
        test::check(std::ranges::all_of(counts, [](auto count) { return count == 0; }),
                    "an empty region must clear the histogram");
        std::vector<std::uint32_t> short_counts(16 * 4);
        test::error(ErrorCode::invalid_argument, "read", "counts",
                    [&] { ctx.read(buffer, short_counts); });
        auto other = Context::create();
        auto foreign = other.create_histogram_buffer();
        test::error(ErrorCode::invalid_resource, "histogram", "destination",
                    [&] { cmd.histogram(image, foreign, {}); });
        test::error(ErrorCode::invalid_resource, "read", "buffer",
                    [&] { ctx.read(foreign, counts); });
        other.destroy(foreign);
        ctx.destroy(buffer);
        test::error(ErrorCode::invalid_resource, "bins", "buffer", [&] { (void)buffer.bins(); });
        test::error(ErrorCode::invalid_resource, "destroy", "resource",
                    [&] { ctx.destroy(buffer); });
        ctx.destroy(small);
        ctx.destroy(image);
    });
    return test::finish();
}
