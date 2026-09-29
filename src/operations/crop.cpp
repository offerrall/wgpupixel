#include "utils/operations.h"

namespace wgpupixel {
using namespace detail;

void Commands::crop(const Image& source, const Image& destination, const CropOptions& options) {
    Operation op(recording_.get(), "crop");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    auto src = op.resource(source.resource_, "source");
    auto dst = op.resource(destination.resource_, "destination");
    op.distinct(*src, *dst);
    auto record = kernel_record(Kernel::crop, src, dst);
    record.parameters.offsets[0] = options.origin.x;
    record.parameters.offsets[1] = options.origin.y;
    op.append({record});
}
} // namespace wgpupixel
