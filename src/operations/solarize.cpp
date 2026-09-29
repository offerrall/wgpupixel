#include "utils/operations.h"

namespace wgpupixel {
using namespace detail;

void Commands::solarize(const Image& destination, const SolarizeOptions& options) {
    Operation op(recording_.get(), "solarize");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& resource = op.resource(destination.resource_, "destination");
    require_finite(op, options.value, "value");
    op.require(options.value >= 0.0f && options.value <= 1.0f, "value",
               "solarize threshold must be between zero and one");
    auto record = kernel_record(Kernel::solarize, resource);
    record.parameters.values[0] = options.value;
    op.append({record});
}
} // namespace wgpupixel
