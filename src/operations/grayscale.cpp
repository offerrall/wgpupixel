#include "utils/operations.h"

namespace wgpupixel {
using namespace detail;

void Commands::grayscale(const Image& destination, const GrayscaleOptions& options) {
    Operation op(recording_.get(), "grayscale");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& pixels = op.resource(destination.resource_, "destination");
    op.append({kernel_record(Kernel::grayscale, pixels)});
}
} // namespace wgpupixel
