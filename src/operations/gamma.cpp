#include "utils/operations.h"

namespace wgpupixel {
using namespace detail;

void Commands::gamma(const Image& destination, const GammaOptions& options) {
    Operation op(recording_.get(), "gamma");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& resource = op.resource(destination.resource_, "destination");
    require_finite(op, options.value, "value");
    op.require(options.value > 0.0f, "value", "gamma must be positive");
    const float exponent = 1.0f / options.value;
    op.require(std::isfinite(exponent), "value", "inverse gamma must be finite");
    auto record = kernel_record(Kernel::gamma, resource);
    record.parameters.values[0] = exponent;
    op.append({record});
}
} // namespace wgpupixel
