#include "utils/operations.h"

namespace wgpupixel {
using namespace detail;

void Commands::copy(const Image& source, const Image& destination,
                      const CopyOptions& options) {
    Operation op(recording_.get(), "copy");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{}, destination.resource_,
                 options.mask != nullptr, options.region);
    const auto& src = op.resource(source.resource_, "source");
    const auto& dst = op.resource(destination.resource_, "destination");
    op.distinct(*src, *dst);
    op.same_size(*src, *dst);
    if (options.mask || options.region) {
        op.append({kernel_record(Kernel::copy_image, src, dst)});
    } else {
        op.append({copy_record(src, dst)});
    }
}
} // namespace wgpupixel
