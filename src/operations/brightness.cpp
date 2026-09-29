#include "utils/operations.h"

namespace wgpupixel {
using namespace detail;

void Commands::brightness(const Image& destination, const BrightnessOptions& options) {
    Operation op(recording_.get(), "brightness");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& pixels = op.resource(destination.resource_, "destination");
    op.require(std::isfinite(options.amount), "amount", "amount must be finite");
    auto record = kernel_record(Kernel::brightness, pixels);
    record.parameters.values[0] = options.amount;
    op.append({record});
}
} // namespace wgpupixel
