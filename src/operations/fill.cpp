#include "utils/operations.h"

namespace wgpupixel {
using namespace detail;

void Commands::fill(const Image& destination, const FillOptions& options) {
    Operation op(recording_.get(), "fill");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& pixels = op.resource(destination.resource_, "destination");
    require_color(op, options.color, "color");
    auto record = kernel_record(Kernel::fill, pixels);
    record.parameters.values = {options.color.r, options.color.g, options.color.b, options.color.a};
    op.append({record});
}
} // namespace wgpupixel
