#include "utils/sizes.h"
#include "runtime.h"
#include "operations/filter_pyramid.h"
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <limits>

namespace wgpupixel {
namespace {
constexpr auto maximum = std::numeric_limits<std::int32_t>::max();
void require(bool condition, std::string_view operation, std::string_view parameter,
             std::string_view message) {
    if (!condition) {
        detail::fail(ErrorCode::invalid_argument, operation, parameter, message);
    }
}
void require_size(ImageSize size, std::string_view operation) {
    require(size.width > 0 && size.height > 0 && size.width <= maximum && size.height <= maximum,
            operation, "source", "source dimensions must be positive 32-bit signed integers");
    if (std::uint64_t(size.width) * size.height > UINT32_MAX)
        detail::fail(ErrorCode::capacity, operation, "source", "pixel count exceeds shader address capacity");
}
} // namespace

detail::ShadowGeometry detail::shadow_geometry(ImageSize source, const DropShadowOptions& options,
                                               std::string_view operation) {
    const auto offset = options.offset;
    const auto radius = options.radius;
    const auto expand = options.expand;
    require(std::isfinite(options.sigma) && options.sigma > 0, operation, "sigma",
            "sigma must be finite and positive");
    const auto color = options.color;
    require(std::isfinite(color.r) && std::isfinite(color.g) && std::isfinite(color.b) &&
                std::isfinite(color.a) && color.a >= 0 && color.a <= 1,
            operation, "color", "color must be finite with alpha between zero and one");
    require_size(source, operation);
    require(radius >= 0 && radius <= maximum, operation, "radius",
            "radius must be a nonnegative 32-bit signed integer");
    const ImageSize padded{source.width + 2 * radius, source.height + 2 * radius};
    require(padded.width <= maximum && padded.height <= maximum, operation, "radius",
            "padded dimensions exceed supported range");
    const auto ox = std::int64_t(offset.x), oy = std::int64_t(offset.y);
    const ImageSize destination =
        expand ? ImageSize{padded.width + std::abs(ox), padded.height + std::abs(oy)} : source;
    require(destination.width <= maximum && destination.height <= maximum, operation, "offset",
            "expanded dimensions exceed supported range");
    const auto px = expand ? radius + std::max<std::int64_t>(0, -ox) : 0;
    const auto py = expand ? radius + std::max<std::int64_t>(0, -oy) : 0;
    const auto sx = px + ox - radius, sy = py + oy - radius;
    require(sx >= std::numeric_limits<std::int32_t>::min() && sx <= maximum &&
                sy >= std::numeric_limits<std::int32_t>::min() && sy <= maximum,
            operation, "offset", "shadow position exceeds supported range");
    return {{destination, padded},
            {static_cast<std::int32_t>(px), static_cast<std::int32_t>(py)},
            {static_cast<std::int32_t>(sx), static_cast<std::int32_t>(sy)}};
}

OperationRequirements gaussian_blur_requirements(ImageSize source,
                                                 const GaussianBlurOptions& options) {
    require(options.radius >= 0 && options.radius <= maximum, "gaussian_blur_requirements",
            "radius", "radius must be a nonnegative 32-bit signed integer");
    require(std::isfinite(options.sigma) && options.sigma > 0, "gaussian_blur_requirements",
            "sigma", "sigma must be finite and positive");
    require_size(source, "gaussian_blur_requirements");
    const double support = std::min(double(options.radius), std::floor(double(options.sigma) * 16));
    if (options.radius == 0) {
        return {source, {}};
    }
    return {source, support <= 128 ? detail::workspace_plan({std::uint64_t(source.width) *
                                                             source.height * detail::pixel_bytes},
                                                            "gaussian_blur_requirements")
                                   : detail::gaussian_workspace(source, support,
                                                                "gaussian_blur_requirements")};
}

OperationRequirements drop_shadow_requirements(ImageSize source, const DropShadowOptions& options) {
    const auto sizes =
        detail::shadow_geometry(source, options, "drop_shadow_requirements").requirements;
    require_size(sizes.shadow, "drop_shadow_requirements");
    const auto bytes =
        std::uint64_t(sizes.shadow.width) * sizes.shadow.height * detail::pixel_bytes;
    return {sizes.destination, detail::workspace_plan({bytes, options.radius ? bytes : 0},
                                                      "drop_shadow_requirements")};
}
} // namespace wgpupixel
