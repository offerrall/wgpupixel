#include "utils/operations.h"

namespace wgpupixel {
using namespace detail;

void Commands::contrast(const Image& destination, const ContrastOptions& options) {
    Operation op(recording_.get(), "contrast");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& resource = op.resource(destination.resource_, "destination");
    require_finite(op, options.factor, "factor");
    op.require(options.factor >= 0.0f, "factor", "factor must be nonnegative");
    auto record = kernel_record(Kernel::contrast, resource);
    record.parameters.values[0] = options.factor;
    op.append({record});
}
} // namespace wgpupixel
