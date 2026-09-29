#pragma once

#include "test.h"
#include <array>
#include <numbers>

namespace test {

inline double reconstruction_weight(double x, wgpupixel::ResizeFilter filter) {
    using wgpupixel::ResizeFilter;
    x = std::abs(x);
    if (filter == ResizeFilter::bilinear) {
        return std::max(1.0 - x, 0.0);
    }
    if (filter == ResizeFilter::bicubic) {
        if (x <= 1) {
            return 1 - 2.5 * x * x + 1.5 * x * x * x;
        }
        if (x < 2) {
            return 2 - 4 * x + 2.5 * x * x - 0.5 * x * x * x;
        }
        return 0;
    }
    if (x >= 3) {
        return 0;
    }
    if (x < 1e-12) {
        return 1;
    }
    const double p = std::numbers::pi * x;
    return 3 * std::sin(p) * std::sin(p / 3) / (p * p);
}

// Independent double-precision reconstruction of a zero-extended image.
// Out-of-image samples contribute zero, but retain their normalization weight.
inline std::array<float, 4> transparent_sample(std::span<const float> pixels, int width, int height,
                                               double x, double y, wgpupixel::ResizeFilter filter) {
    using wgpupixel::ResizeFilter;
    const auto sample = [&](int xx, int yy, int c) -> double {
        if (xx < 0 || yy < 0 || xx >= width || yy >= height) {
            return 0;
        }
        return pixels[(yy * width + xx) * 4 + c];
    };
    std::array<float, 4> result{};
    if (filter == ResizeFilter::nearest) {
        for (int c = 0; c < 4; ++c) {
            result[c] = float(sample(int(std::floor(x + 0.5)), int(std::floor(y + 0.5)), c));
        }
        return result;
    }
    const int radius = filter == ResizeFilter::bilinear  ? 1
                       : filter == ResizeFilter::bicubic ? 2
                                                         : 3;
    double total = 0;
    std::array<double, 4> sum{};
    for (int yy = int(std::ceil(y - radius)); yy <= y + radius; ++yy) {
        for (int xx = int(std::ceil(x - radius)); xx <= x + radius; ++xx) {
            const double weight =
                reconstruction_weight(x - xx, filter) * reconstruction_weight(y - yy, filter);
            total += weight;
            for (int c = 0; c < 4; ++c) {
                sum[c] += weight * sample(xx, yy, c);
            }
        }
    }
    for (int c = 0; c < 4; ++c) {
        result[c] = float(sum[c] / total);
    }
    return result;
}

} // namespace test
