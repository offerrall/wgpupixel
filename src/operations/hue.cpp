#include "utils/operations.h"

namespace wgpupixel {
using namespace detail;

void Commands::hue(const Image& destination, const HueOptions& options) {
    Operation op(recording_.get(), "hue");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& resource = op.resource(destination.resource_, "destination");
    require_finite(op, options.degrees, "degrees");
    float normalized = std::fmod(options.degrees, 360.0f);
    if (normalized < 0.0f) {
        normalized += 360.0f;
    }
    auto record = kernel_record(Kernel::hue, resource);
    record.parameters.values[0] = normalized;
    op.append({record});
}
} // namespace wgpupixel
