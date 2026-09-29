#include "utils/operations.h"

namespace wgpupixel {
using namespace detail;

void Commands::opacity(const Image& destination, const OpacityOptions& options) {
    Operation op(recording_.get(), "opacity");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& resource = op.resource(destination.resource_, "destination");
    require_finite(op, options.factor, "factor");
    op.require(options.factor >= 0.0f && options.factor <= 1.0f, "factor",
               "opacity factor must be between zero and one");
    auto record = kernel_record(Kernel::opacity, resource);
    record.parameters.values[0] = options.factor;
    op.append({record});
}
} // namespace wgpupixel
