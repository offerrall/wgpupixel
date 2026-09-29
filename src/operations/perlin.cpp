#include "utils/operations.h"
#include <algorithm>
#include <limits>

namespace wgpupixel {
using namespace detail;

void Commands::perlin(const Image& destination, const PerlinOptions& options) {
    Operation op(recording_.get(), "perlin");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    auto pixels = op.resource(destination.resource_, "destination");
    require_finite(op, options.scale, "scale");
    op.require(options.scale >= std::numeric_limits<float>::min(), "scale",
               "scale must be positive and at least the minimum normal float");
    op.require(options.octaves >= 1 && options.octaves <= 16, "octaves",
               "octaves must be between one and sixteen");
    require_finite(op, options.persistence, "persistence");
    op.require(options.persistence >= 0 && options.persistence <= 1, "persistence",
               "persistence must be in [0, 1]");
    require_finite(op, options.lacunarity, "lacunarity");
    op.require(options.lacunarity >= 1, "lacunarity", "lacunarity must be at least one");
    require_finite(op, options.offset_x, "offset_x");
    require_finite(op, options.offset_y, "offset_y");
    require_color(op, options.first, "first");
    require_color(op, options.second, "second");
    // Match the shader's rounded endpoint arithmetic before bounding every octave.
    const float domain = std::max(
        {std::abs(options.offset_x / options.scale), std::abs(options.offset_y / options.scale),
         std::abs((float(pixels->width - 1) + options.offset_x) / options.scale),
         std::abs((float(pixels->height - 1) + options.offset_y) / options.scale)});
    float frequency = 1;
    for (std::int64_t octave = 0; octave < options.octaves; ++octave) {
        op.require(std::isfinite(frequency), "lacunarity", "octave frequency must remain finite");
        op.require(std::isfinite(domain) && double(domain) * frequency <= 1048576.0, "scale",
                   "mapped coordinates must remain within [-2^20, 2^20] at every octave");
        if (octave + 1 < options.octaves) {
            frequency *= options.lacunarity;
        }
    }
    auto record = kernel_record(Kernel::perlin, pixels);
    record.parameters.values = {options.scale, options.persistence, options.lacunarity, 0};
    record.parameters.extra = {options.offset_x, options.offset_y, 0, 0};
    record.parameters.reserved[0] = options.seed;
    record.parameters.reserved[1] = static_cast<std::uint32_t>(options.octaves);
    record.parameters.color1 = rgba(options.first);
    record.parameters.color2 = rgba(options.second);
    op.append({record});
}

} // namespace wgpupixel
