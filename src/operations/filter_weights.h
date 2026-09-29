#pragma once
#include "utils/operations.h"

namespace wgpupixel::detail {
inline void filter_weights(Operation& op, Record& horizontal, Record& vertical, float sigma,
                           std::int64_t radius) {
    const auto count = std::size_t(radius) + 1;
    auto weights = op.data(horizontal, count, "radius");
    double normalization = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto ratio = double(i) / sigma;
        const float weight = float(std::exp(-0.5 * ratio * ratio));
        weights[i] = std::bit_cast<std::uint32_t>(weight);
        normalization += (i == 0 ? 1 : 2) * double(weight);
    }
    for (auto& word : weights) {
        word =
            std::bit_cast<std::uint32_t>(float(std::bit_cast<float>(word) * (0.5 / normalization)));
    }
    // Both records bind the same immutable slice.
    vertical.data_offset = horizontal.data_offset;
    vertical.data_count = horizontal.data_count;
}
} // namespace wgpupixel::detail
