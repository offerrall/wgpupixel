#include "utils/operations.h"
#include <numbers>

namespace wgpupixel {
using namespace detail;

void Commands::stripes(const Image& destination, const StripesOptions& options) {
    Operation op(recording_.get(), "stripes");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    auto pixels = op.resource(destination.resource_, "destination");
    require_finite(op, options.angle, "angle");
    require_finite(op, options.spacing, "spacing");
    require_finite(op, options.width, "width");
    require_finite(op, options.offset, "offset");
    op.require(options.spacing >= 1, "spacing", "spacing must be at least one pixel");
    op.require(options.width >= 0 && options.width <= options.spacing, "width",
               "width must be between zero and spacing");
    require_color(op, options.first, "first");
    require_color(op, options.second, "second");
    const double radians = std::remainder(double(options.angle), 360.0) * std::numbers::pi / 180.0;
    auto record = kernel_record(Kernel::stripes, pixels);
    record.parameters.values = {static_cast<float>(std::cos(radians)),
                                static_cast<float>(std::sin(radians)), options.spacing,
                                options.width};
    record.parameters.extra[0] =
        static_cast<float>(std::remainder(double(options.offset), double(options.spacing)));
    record.parameters.color1 = rgba(options.first);
    record.parameters.color2 = rgba(options.second);
    op.append({record});
}

} // namespace wgpupixel
