#pragma once
// CPU references shared by the histogram and statistics checks.
#include "../test.h"
#include <array>
#include <random>

namespace analysis {
using namespace wgpupixel;

struct Measured {
    std::array<double, 5> values; // red, green, blue, alpha, luminosity
    bool color;
};

inline double encode(double linear) {
    linear = std::clamp(linear, 0.0, 1.0);
    return linear <= 0.0031308 ? 12.92 * linear : 1.055 * std::pow(linear, 1 / 2.4) - 0.055;
}

inline Measured measure(const float* pixel, ColorEncoding space) {
    const double alpha = pixel[3];
    std::array<double, 3> rgb{};
    if (alpha > 0) {
        for (int c = 0; c < 3; ++c) {
            rgb[c] = double(pixel[c]) / alpha;
        }
    }
    if (space == ColorEncoding::srgb) {
        for (auto& value : rgb) {
            value = encode(value);
        }
        const double luminosity = 0.30 * rgb[0] + 0.59 * rgb[1] + 0.11 * rgb[2];
        return {{rgb[0], rgb[1], rgb[2], std::clamp(alpha, 0.0, 1.0), luminosity}, alpha > 0};
    }
    const double luminosity = 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
    return {{rgb[0], rgb[1], rgb[2], alpha, luminosity}, alpha > 0};
}

inline std::uint32_t bin(double value, std::uint32_t bins) {
    const double scaled = std::clamp(value, 0.0, 1.0) * (bins - 1);
    return std::min<std::uint32_t>(static_cast<std::uint32_t>(scaled + 0.5), bins - 1);
}

// True when float evaluation on the GPU could round the value into a neighboring bin.
inline bool ambiguous(double value, std::uint32_t bins) {
    const double scaled = std::clamp(value, 0.0, 1.0) * (bins - 1);
    return std::abs(scaled - std::floor(scaled) - 0.5) < 0.02;
}

// Random premultiplied linear pixels with transparent, translucent, HDR and negative colors.
// Pixels whose measurement would sit on a bin boundary for any listed bin count are redrawn.
inline std::vector<float> pixels(std::size_t count, std::uint32_t seed,
                                 std::initializer_list<std::uint32_t> bins = {}) {
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> color(-0.2f, 1.3f), unit(0.0f, 1.0f);
    std::vector<float> result;
    while (result.size() < count * 4) {
        const float draw = unit(random);
        const float alpha = draw < 0.1f   ? 0.0f
                            : draw < 0.4f ? 1.0f
                                          : std::max(unit(random), 0.004f);
        std::array<float, 4> pixel{color(random) * alpha, color(random) * alpha,
                                   color(random) * alpha, alpha};
        bool valid = true;
        for (auto space : {ColorEncoding::srgb, ColorEncoding::linear}) {
            const auto measured = measure(pixel.data(), space);
            for (auto count_of_bins : bins) {
                for (double value : measured.values) {
                    valid = valid && !ambiguous(value, count_of_bins);
                }
            }
        }
        if (valid) {
            result.insert(result.end(), pixel.begin(), pixel.end());
        }
    }
    return result;
}

inline std::vector<std::uint8_t> coverage(std::size_t count, std::uint32_t seed) {
    std::mt19937 random(seed);
    constexpr std::array<std::uint8_t, 8> levels{0, 1, 64, 127, 128, 129, 200, 255};
    std::vector<std::uint8_t> result(count);
    for (auto& value : result) {
        value = levels[random() % levels.size()];
    }
    return result;
}

inline void upload(Context& ctx, const Image& image, std::span<const float> premultiplied) {
    auto buffer = ctx.create_upload_buffer(image, {.format = TransferFormat::rgba32_float});
    ctx.write(buffer, {reinterpret_cast<const std::uint8_t*>(premultiplied.data()),
                       premultiplied.size_bytes()});
    auto cmd = ctx.create_commands(1);
    cmd.upload(buffer, image);
    ctx.submit_and_wait(cmd);
    ctx.destroy(buffer);
}

inline void upload(Context& ctx, const Mask& mask, std::span<const std::uint8_t> bytes) {
    auto buffer = ctx.create_upload_buffer(mask);
    ctx.write(buffer, bytes);
    auto cmd = ctx.create_commands(1);
    cmd.upload(buffer, mask);
    ctx.submit_and_wait(cmd);
    ctx.destroy(buffer);
}

// Whether pixel (x, y) is measured under an optional mask and region.
inline bool selected(std::int64_t x, std::int64_t y, std::int64_t width,
                     std::span<const std::uint8_t> mask, const std::optional<Rect>& region) {
    if (region && (x < region->x || y < region->y || x >= std::int64_t(region->x) + region->width ||
                   y >= std::int64_t(region->y) + region->height)) {
        return false;
    }
    return mask.empty() || mask[y * width + x] >= 128;
}
} // namespace analysis
