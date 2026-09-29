#include "utils/operations.h"

namespace wgpupixel {
using namespace detail;

void Commands::flip(const Image& source, const Image& destination, const FlipOptions& options) {
    Operation op(recording_.get(), "flip");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    auto src = op.resource(source.resource_, "source");
    auto dst = op.resource(destination.resource_, "destination");
    op.distinct(*src, *dst);
    op.same_size(*src, *dst);
    op.require(std::to_underlying(options.direction) <= std::to_underlying(FlipDirection::both),
               "direction", "flip direction is invalid");
    auto record = kernel_record(Kernel::flip, src, dst);
    record.parameters.reserved[0] = std::to_underlying(options.direction);
    op.append({record});
}
} // namespace wgpupixel
