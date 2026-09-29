#include "utils/operations.h"
#include <limits>

namespace wgpupixel {
using namespace detail;

void Commands::grid(const Image& destination, const GridOptions& options) {
    Operation op(recording_.get(), "grid");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    auto pixels = op.resource(destination.resource_, "destination");
    op.require(options.spacing > 0 && options.spacing <= std::numeric_limits<std::int32_t>::max(),
               "spacing", "spacing must be a positive 32-bit signed integer");
    op.require(options.line_width > 0 && options.line_width <= options.spacing, "line_width",
               "line width must be positive and no greater than spacing");
    require_color(op, options.color, "color");
    require_color(op, options.background, "background");
    auto record = kernel_record(Kernel::grid, pixels);
    record.parameters.color1 = rgba(options.color);
    record.parameters.color2 = rgba(options.background);
    record.parameters.reserved[0] = static_cast<std::uint32_t>(options.spacing);
    record.parameters.reserved[1] = static_cast<std::uint32_t>(options.line_width);
    const std::int64_t offsets[] = {options.offset.x, options.offset.y};
    for (unsigned axis = 0; axis < 2; ++axis) {
        auto remainder = offsets[axis] % options.spacing;
        if (remainder < 0) {
            remainder += options.spacing;
        }
        record.parameters.offsets[axis] = static_cast<std::int32_t>(remainder);
    }
    op.append({record});
}

} // namespace wgpupixel
