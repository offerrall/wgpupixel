#include "utils/operations.h"

namespace wgpupixel {
using namespace detail;

void Commands::vibrance(const Image& destination, const VibranceOptions& options) {
    Operation op(recording_.get(), "vibrance");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& resource = op.resource(destination.resource_, "destination");
    require_finite(op, options.amount, "amount");
    op.require(options.amount >= -1.0f && options.amount <= 1.0f, "amount",
               "vibrance amount must be between minus one and one");
    auto record = kernel_record(Kernel::vibrance, resource);
    record.parameters.values[0] = options.amount;
    op.append({record});
}
} // namespace wgpupixel
