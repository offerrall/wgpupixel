#include "utils/operations.h"
#include <algorithm>
#include <numbers>

namespace wgpupixel {
using namespace detail;

void Commands::polygon(const Image& destination, const PolygonOptions& options) {
    Operation op(recording_.get(), "polygon");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    auto pixels = op.resource(destination.resource_, "destination");
    op.require(options.sides >= 3 && options.sides <= 1024, "sides",
               "sides must be between three and 1024");
    require_color(op, options.color, "color");
    require_color(op, options.background, "background");
    require_finite(op, options.rotation, "rotation");
    require_finite(op, options.softness, "softness");
    op.require(options.softness >= 0, "softness", "softness must be nonnegative");
    const double radians = std::remainder(double(options.rotation), 360.0) * std::numbers::pi / 180;
    const double half_sector = std::numbers::pi / double(options.sides);
    auto record = kernel_record(Kernel::polygon, pixels);
    record.parameters.color1 = rgba(options.color);
    record.parameters.color2 = rgba(options.background);
    record.parameters.values = {float(std::cos(radians)), float(std::sin(radians)),
                                float(half_sector * 2), std::max(1.0f, options.softness)};
    record.parameters.extra = {float(std::cos(half_sector)), float(std::sin(half_sector)), 0, 0};
    op.append({record});
}

} // namespace wgpupixel
