#include "utils/operations.h"

namespace wgpupixel {
using namespace detail;

void Commands::sobel(const Image& source, const Image& destination, const SobelOptions& options) {
    Operation op(recording_.get(), "sobel");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    auto src = op.resource(source.resource_, "source");
    auto dst = op.resource(destination.resource_, "destination");
    op.distinct(*src, *dst);
    op.same_size(*src, *dst);
    op.append({kernel_record(Kernel::sobel, src, dst)});
}
} // namespace wgpupixel
