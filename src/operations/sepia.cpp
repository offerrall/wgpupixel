#include "utils/operations.h"

namespace wgpupixel {
using namespace detail;

void Commands::sepia(const Image& destination, const SepiaOptions& options) {
    Operation op(recording_.get(), "sepia");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& resource = op.resource(destination.resource_, "destination");
    require_finite(op, options.intensity, "intensity");
    op.require(options.intensity >= 0.0f && options.intensity <= 1.0f, "intensity",
               "sepia intensity must be between zero and one");
    auto record = kernel_record(Kernel::sepia, resource);
    record.parameters.values[0] = options.intensity;
    op.append({record});
}
} // namespace wgpupixel
