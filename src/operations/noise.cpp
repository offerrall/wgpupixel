#include "utils/operations.h"

namespace wgpupixel {
using namespace detail;

void Commands::noise(const Image& destination, const NoiseOptions& options) {
    Operation op(recording_.get(), "noise");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    auto pixels = op.resource(destination.resource_, "destination");
    auto record = kernel_record(Kernel::noise, pixels);
    record.parameters.reserved[0] = options.seed;
    record.parameters.reserved[1] = options.monochrome ? 1u : 0u;
    op.append({record});
}

} // namespace wgpupixel
