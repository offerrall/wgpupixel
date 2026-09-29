#include "utils/operations.h"
namespace wgpupixel {
using namespace detail;
void Commands::vignette(const Image& destination, const VignetteOptions& options) {
    Operation op(recording_.get(), "vignette");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& pixels = op.resource(destination.resource_, "destination");
    require_finite(op, options.radius, "radius");
    require_finite(op, options.softness, "softness");
    op.require(options.radius >= 0, "radius", "radius must be nonnegative");
    op.require(options.softness >= 0, "softness", "softness must be nonnegative");
    require_color(op, options.color, "color");
    auto record = kernel_record(Kernel::vignette, pixels);
    record.parameters.values = {options.radius, options.softness, 0, 0};
    record.parameters.color1 = rgba(options.color);
    op.append({record});
}
} // namespace wgpupixel
