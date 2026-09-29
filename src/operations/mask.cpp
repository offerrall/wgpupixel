#include "utils/operations.h"

namespace wgpupixel {
using namespace detail;
void Commands::apply_mask(const Mask& source, const Image& destination,
                          const ApplyMaskOptions& options) {
    Operation op(recording_.get(), "apply_mask");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& src = op.resource(source.resource_, "source", ResourceKind::mask);
    const auto& dst = op.resource(destination.resource_, "destination");
    op.distinct(*src, *dst);
    auto record = kernel_record(Kernel::mask, src, dst);
    record.parameters.offsets[0] = options.position.x;
    record.parameters.offsets[1] = options.position.y;
    op.append({record});
}
} // namespace wgpupixel
