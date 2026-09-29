#pragma once
#include "commands.h"
#include <cmath>
#include <utility>

namespace wgpupixel::detail {
inline void require_finite(const Operation& op, float value, std::string_view parameter) {
    op.require(std::isfinite(value), parameter, "value must be finite");
}
inline void require_color(const Operation& op, Color color, std::string_view parameter) {
    op.require(std::isfinite(color.r) && std::isfinite(color.g) && std::isfinite(color.b) &&
                   std::isfinite(color.a) && color.a >= 0 && color.a <= 1,
               parameter, "color must be finite with alpha between zero and one");
}
inline std::array<float, 4> rgba(Color color) {
    return {color.r, color.g, color.b, color.a};
}
inline void require_filter(const Operation& op, ResizeFilter filter) {
    op.require(std::to_underlying(filter) <= std::to_underlying(ResizeFilter::lanczos), "filter",
               "resize filter is invalid");
}
// Validates a transfer rectangle, which must lie inside the image; unlike operation
// regions it is never clipped because it defines the CPU buffer layout.
inline std::array<std::uint32_t, 4> transfer_area(const Operation& op, const Resource& image,
                                                  const std::optional<Rect>& region) {
    if (!region) {
        return {0, 0, image.width, image.height};
    }
    const auto [x, y, width, height] = *region;
    op.require(x >= 0 && y >= 0 && width > 0 && height > 0 &&
                   std::int64_t(x) + width <= image.width &&
                   std::int64_t(y) + height <= image.height,
               "region", "region must be nonempty and inside the image");
    return {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
            static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
}
} // namespace wgpupixel::detail
