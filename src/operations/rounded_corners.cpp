#include "utils/operations.h"
#include <algorithm>
namespace wgpupixel {
using namespace detail;
void Commands::rounded_corners(const Image& destination, const RoundedCornersOptions& options) {
    Operation op(recording_.get(), "rounded_corners");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& pixels = op.resource(destination.resource_, "destination");
    require_finite(op, options.radius, "radius");
    op.require(options.radius >= 0 &&
                   options.radius <= std::min(pixels->width, pixels->height) * .5f,
               "radius", "radius must be between zero and half the smaller dimension");
    auto record = kernel_record(Kernel::rounded_corners, pixels);
    record.parameters.values[0] = options.radius;
    op.append({record});
}
} // namespace wgpupixel
