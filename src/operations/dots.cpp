#include "utils/operations.h"
#include <algorithm>

namespace wgpupixel {
using namespace detail;

void Commands::dots(const Image& destination, const DotsOptions& options) {
    Operation op(recording_.get(), "dots");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    auto pixels = op.resource(destination.resource_, "destination");
    require_finite(op, options.spacing, "spacing");
    require_finite(op, options.radius, "radius");
    require_finite(op, options.softness, "softness");
    op.require(options.spacing >= 1, "spacing", "spacing must be at least one pixel");
    op.require(options.radius >= 0 && options.radius <= options.spacing * 0.5f, "radius",
               "radius must be between zero and half the spacing");
    op.require(options.softness >= 0, "softness", "softness must be nonnegative");
    require_color(op, options.color, "color");
    require_color(op, options.background, "background");
    auto record = kernel_record(Kernel::dots, pixels);
    record.parameters.color1 = rgba(options.color);
    record.parameters.color2 = rgba(options.background);
    record.parameters.values = {options.spacing, options.radius, std::max(1.0f, options.softness),
                                0};
    record.parameters.extra[0] =
        static_cast<float>(std::remainder(double(options.offset.x), double(options.spacing)));
    record.parameters.extra[1] =
        static_cast<float>(std::remainder(double(options.offset.y), double(options.spacing)));
    op.append({record});
}

} // namespace wgpupixel
