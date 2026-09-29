#include "utils/operations.h"

namespace wgpupixel {
using namespace detail;

void Commands::invert(const Image& destination, const InvertOptions& options) {
    Operation op(recording_.get(), "invert");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& resource = op.resource(destination.resource_, "destination");
    op.append({kernel_record(Kernel::invert, resource)});
}
} // namespace wgpupixel
