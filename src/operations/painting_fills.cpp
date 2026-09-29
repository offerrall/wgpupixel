#include "brush_engine.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace wgpupixel {
using namespace detail;

void Commands::gradient_fill(const Image& destination, const GradientFillOptions& options) {
    Operation op(recording_.get(), "gradient_fill");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& pixels = op.resource(destination.resource_, "destination");
    op.require(std::isfinite(options.start.x) && std::isfinite(options.start.y), "start",
               "start must be finite");
    op.require(std::isfinite(options.end.x) && std::isfinite(options.end.y), "end",
               "end must be finite");
    const double dx = double(options.end.x) - options.start.x;
    const double dy = double(options.end.y) - options.start.y;
    const double squared = dx * dx + dy * dy;
    op.require(squared > 0 && std::isfinite(float(1 / squared)) && float(1 / squared) > 0 &&
                   std::isfinite(float(squared)),
               "end", "end must differ from start by a representable distance");
    op.require(!options.stops.empty(), "stops", "at least one stop is required");
    float previous = 0;
    for (const auto& stop : options.stops) {
        op.require(stop.position >= previous && stop.position <= 1, "stops",
                   "stop positions must be nondecreasing within [0, 1]");
        require_color(op, stop.color, "stops");
        previous = stop.position;
    }
    op.require(std::to_underlying(options.shape) <= std::to_underlying(GradientShape::diamond),
               "shape", "gradient shape is invalid");
    op.require(std::to_underlying(options.extend) <= std::to_underlying(EdgeMode::mirror), "extend",
               "edge mode is invalid");
    op.require(std::to_underlying(options.interpolation) <=
                   std::to_underlying(GradientInterpolation::perceptual),
               "interpolation", "interpolation is invalid");
    require_mode(op, options.mode);
    require_unit(op, options.opacity, "opacity");
    auto record = kernel_record(Kernel::gradient_fill, pixels);
    auto& p = record.parameters;
    p.values = {options.start.x, options.start.y, float(dx), float(dy)};
    p.extra = {float(1 / squared), float(1 / std::sqrt(squared)), options.opacity, 0};
    p.reserved = {std::to_underlying(options.shape), std::to_underlying(options.mode),
                  static_cast<std::uint32_t>(options.stops.size()),
                  (options.reverse ? 1u : 0u) | (options.dither ? 2u : 0u) |
                      (options.interpolation == GradientInterpolation::perceptual ? 4u : 0u) |
                      (options.replace ? 8u : 0u) | (options.preserve_alpha ? 256u : 0u) |
                      std::to_underlying(options.extend) << 4};
    op.require(options.stops.size() <= std::numeric_limits<std::uint32_t>::max() / 5, "stops",
               "too many stops", ErrorCode::capacity);
    const auto words = op.data(record, options.stops.size() * 5, "stops");
    for (std::size_t i = 0; i < options.stops.size(); ++i) {
        const auto& [position, color] = options.stops[i];
        const std::array values{position, color.r, color.g, color.b, color.a};
        std::ranges::transform(values, words.begin() + std::ptrdiff_t(5 * i),
                               [](float value) { return std::bit_cast<std::uint32_t>(value); });
    }
    op.append({record});
}

void Commands::pattern_fill(const Image& pattern, const Image& destination,
                            const PatternFillOptions& options) {
    Operation op(recording_.get(), "pattern_fill");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& image = op.resource(pattern.resource_, "pattern");
    const auto& pixels = op.resource(destination.resource_, "destination");
    op.distinct(*image, *pixels, "pattern");
    const auto inverse_transform = inverse(options.transform);
    op.require(inverse_transform.has_value(), "transform", "transform must be invertible");
    require_mode(op, options.mode);
    require_unit(op, options.opacity, "opacity");
    auto record = kernel_record(Kernel::pattern_fill, image, pixels);
    auto& p = record.parameters;
    const auto& m = *inverse_transform;
    p.values[0] = options.opacity;
    p.reserved[0] = std::to_underlying(options.mode);
    p.reserved[3] = options.preserve_alpha ? 256u : 0u;
    p.extra = {m.a, m.b, m.c, m.d};
    p.extra2 = {m.e, m.f, 0, 0};
    op.append({record});
}

void Commands::paint_bucket(const Image& destination, const PaintBucketOptions& options) {
    Operation op(recording_.get(), "paint_bucket");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& pixels = op.resource(destination.resource_, "destination");
    if (options.match_seed) {
        op.require(options.seed.x >= 0 && options.seed.y >= 0 &&
                       std::uint32_t(options.seed.x) < pixels->width &&
                       std::uint32_t(options.seed.y) < pixels->height,
                   "seed", "seed must lie inside the image");
        require_unit(op, options.tolerance, "tolerance");
        require_unit(op, options.softness, "softness");
    }
    require_color(op, options.color, "color");
    require_mode(op, options.mode);
    require_unit(op, options.opacity, "opacity");
    // Every pixel compares against the seed's original color: the first pass skips the
    // seed and the second fills it alone.
    auto record = kernel_record(Kernel::paint_bucket, pixels);
    auto& p = record.parameters;
    p.offsets = {options.seed.x, options.seed.y, 0, 0};
    p.values = {options.tolerance, options.softness, options.opacity, 0};
    p.color1 = rgba(options.color);
    p.reserved = {std::to_underlying(options.mode), options.match_seed ? 0u : 1u,
                  options.match_seed ? 1u : 0u, options.preserve_alpha ? 256u : 0u};
    if (!options.match_seed) {
        op.append({record});
        return;
    }
    auto seed = record;
    seed.parameters.reserved[2] = 0;
    seed.parameters.dispatch = {static_cast<std::uint32_t>(options.seed.x),
                                static_cast<std::uint32_t>(options.seed.y), 1, 1};
    op.append({record, seed});
}
} // namespace wgpupixel
