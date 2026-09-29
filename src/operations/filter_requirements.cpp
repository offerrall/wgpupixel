#include "runtime.h"
#include "utils/workspace.h"
#include "filter_sequence.h"
#include "filter_median.h"
#include <cmath>
#include <limits>
namespace wgpupixel {
namespace {
void require(bool condition, std::string_view operation, std::string_view parameter) {
    if (!condition) {
        detail::fail(ErrorCode::invalid_argument, operation, parameter,
                     "value is outside the supported range");
    }
}
void size(ImageSize source, std::string_view name) {
    require(source.width > 0 && source.height > 0 &&
                source.width <= std::numeric_limits<std::int32_t>::max() &&
                source.height <= std::numeric_limits<std::int32_t>::max(),
            name, "source");
    if (std::uint64_t(source.width) * source.height > UINT32_MAX)
        detail::fail(ErrorCode::capacity, name, "source", "pixel count exceeds shader address capacity");
}
} // namespace
OperationRequirements median_requirements(ImageSize source, const MedianOptions& options) {
    constexpr std::string_view name = "median_requirements";
    size(source, name);
    require(options.radius >= 0 && options.radius <= 500, name, "radius");
    WorkspacePlan plan;
    if (options.radius > 3) {
        const auto width = std::min<std::int64_t>(1024, source.width) + 2 * options.radius;
        const auto height = std::min<std::int64_t>(1024, source.height) + 2 * options.radius;
        const auto count = std::uint64_t((width * height + 31) / 32 * 32);
        const auto words = count / 32;
        const auto planes = 8u * (std::bit_width(std::uint32_t(width)) + 1);
        const auto aligned = [](std::uint64_t n) { return (n + 15) / 16 * 16; };
        for (int i = 0; i < 4; ++i)
            detail::workspace_buffer(plan, aligned(planes * words * 8), name);
        detail::workspace_buffer(plan, aligned((words + 63) / 64 * 4), name);
        for (int i = 0; i < 2; ++i) {
            detail::workspace_buffer(plan, aligned(count * 8), name);
            detail::workspace_buffer(plan, aligned((count + 1) * 4), name);
        }
    }
    return {source, std::move(plan)};
}
OperationRequirements motion_blur_requirements(ImageSize source, const MotionBlurOptions& options) {
    constexpr std::string_view name = "motion_blur_requirements";
    size(source, name);
    require(std::isfinite(options.angle) && std::abs(options.angle) <= 360, name, "angle");
    require(std::isfinite(options.distance) && options.distance >= 0 && options.distance <= 4096,
            name, "distance");
    const auto bytes =
        options.distance <= 32 ? 0 : 16 * std::uint64_t(source.width) * source.height;
    return {source, detail::workspace_plan({bytes, bytes}, name)};
}
OperationRequirements radial_blur_requirements(ImageSize source, const RadialBlurOptions& options) {
    constexpr std::string_view name = "radial_blur_requirements";
    size(source, name);
    require(std::isfinite(options.amount) && options.amount >= 0 && options.amount <= 100, name,
            "amount");
    require(std::to_underlying(options.mode) <= 1, name, "mode");
    const Point center = options.center == Point{-1, -1}
                             ? Point{source.width * .5f, source.height * .5f}
                             : options.center;
    require(std::isfinite(center.x) && std::isfinite(center.y) && center.x >= 0 && center.y >= 0 &&
                center.x <= source.width && center.y <= source.height,
            name, "center");
    const double distance =
        std::hypot(std::max(std::abs(center.x - .5), std::abs(source.width - .5 - center.x)),
                   std::max(std::abs(center.y - .5), std::abs(source.height - .5 - center.y)));
    const bool spin = options.mode == RadialBlurMode::spin;
    const double extent = distance * options.amount * (spin ? .017453292519943295 : .01);
    const auto bytes = extent <= 32 ? 0 : 16 * std::uint64_t(source.width) * source.height;
    return {source, detail::workspace_plan({bytes, bytes}, name)};
}
OperationRequirements surface_blur_requirements(ImageSize source,
                                                const SurfaceBlurOptions& options) {
    constexpr std::string_view name = "surface_blur_requirements";
    size(source, name);
    require(options.radius >= 0 && options.radius <= 100, name, "radius");
    require(std::isfinite(options.threshold) && options.threshold >= 0 && options.threshold <= 255,
            name, "threshold");
    const auto bytes = options.radius <= 3 || options.threshold == 0
                           ? 0
                           : 16 * std::uint64_t(source.width) * source.height;
    return {source, detail::workspace_plan({bytes, bytes}, name)};
}
OperationRequirements box_blur_requirements(ImageSize source, const BoxBlurOptions& options) {
    constexpr std::string_view name = "box_blur_requirements";
    size(source, name);
    require(options.radius >= 0 && options.radius <= 1024, name, "radius");
    return {source,
            detail::workspace_plan({16 * std::uint64_t(source.width) * source.height}, name)};
}
OperationRequirements minimum_requirements(ImageSize source, const MorphologyOptions& options) {
    constexpr std::string_view name = "minimum_requirements";
    size(source, name);
    require(options.radius >= 0 && options.radius <= 500, name, "radius");
    require(std::to_underlying(options.shape) <= 1, name, "shape");
    if (options.shape == MorphologyShape::round && options.radius <= 10) {
        return {source, {}};
    }
    const auto bytes = 16 * std::uint64_t(source.width) * source.height;
    const auto second =
        detail::extrema_passes(int(options.radius), options.shape, 1).size() > 2 ? bytes : 0;
    return {source, detail::workspace_plan({bytes, second}, name)};
}
OperationRequirements maximum_requirements(ImageSize source, const MorphologyOptions& options) {
    constexpr std::string_view name = "maximum_requirements";
    size(source, name);
    require(options.radius >= 0 && options.radius <= 500, name, "radius");
    require(std::to_underlying(options.shape) <= 1, name, "shape");
    if (options.shape == MorphologyShape::round && options.radius <= 10) {
        return {source, {}};
    }
    const auto bytes = 16 * std::uint64_t(source.width) * source.height;
    const auto second =
        detail::extrema_passes(int(options.radius), options.shape, 1).size() > 2 ? bytes : 0;
    return {source, detail::workspace_plan({bytes, second}, name)};
}
OperationRequirements unsharp_mask_requirements(ImageSize source,
                                                const UnsharpMaskOptions& options) {
    constexpr std::string_view name = "unsharp_mask_requirements";
    size(source, name);
    require(options.radius >= 0 && options.radius <= 1000, name, "radius");
    require(options.amount >= 0 && options.amount <= 500, name, "amount");
    require(options.threshold >= 0 && options.threshold <= 255, name, "threshold");
    const auto bytes = 16 * std::uint64_t(source.width) * source.height;
    auto plan = options.radius <= 32
                    ? detail::workspace_plan({bytes}, name)
                    : detail::gaussian_workspace(source, std::ceil(3 * options.radius), name);
    detail::workspace_buffer(plan, bytes, name);
    return {source, std::move(plan)};
}
OperationRequirements high_pass_requirements(ImageSize source, const HighPassOptions& options) {
    constexpr std::string_view name = "high_pass_requirements";
    size(source, name);
    require(options.radius >= 0 && options.radius <= 1000, name, "radius");
    const auto bytes = 16 * std::uint64_t(source.width) * source.height;
    auto plan = options.radius <= 32
                    ? detail::workspace_plan({bytes}, name)
                    : detail::gaussian_workspace(source, std::ceil(3 * options.radius), name);
    detail::workspace_buffer(plan, bytes, name);
    return {source, std::move(plan)};
}
} // namespace wgpupixel
