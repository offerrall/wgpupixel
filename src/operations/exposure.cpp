#include "utils/operations.h"

namespace wgpupixel {
using namespace detail;

void Commands::exposure(const Image& destination, const ExposureOptions& options) {
    Operation op(recording_.get(), "exposure");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& resource = op.resource(destination.resource_, "destination");
    require_finite(op, options.stops, "stops");
    const float gain = std::exp2(options.stops);
    // WebGPU implementations may flush subnormal values to zero. Keep the gain
    // normal and finite without imposing a display-range clamp on destination RGB.
    op.require(std::isnormal(gain), "stops", "2^stops must be a normal finite float32 value");
    auto record = kernel_record(Kernel::exposure, resource);
    record.parameters.values[0] = gain;
    op.append({record});
}
} // namespace wgpupixel
