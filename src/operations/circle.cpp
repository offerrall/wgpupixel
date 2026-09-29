#include "utils/operations.h"
#include <algorithm>

namespace wgpupixel {
using namespace detail;

void Commands::circle(const Image& destination, const CircleOptions& options) {
    Operation op(recording_.get(), "circle");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    auto pixels = op.resource(destination.resource_, "destination");
    require_color(op, options.color, "color");
    require_color(op, options.background, "background");
    require_finite(op, options.softness, "softness");
    op.require(options.softness >= 0, "softness", "softness must be nonnegative");
    auto record = kernel_record(Kernel::circle, pixels);
    record.parameters.color1 = rgba(options.color);
    record.parameters.color2 = rgba(options.background);
    record.parameters.values[0] = std::max(1.0f, options.softness);
    op.append({record});
}

} // namespace wgpupixel
