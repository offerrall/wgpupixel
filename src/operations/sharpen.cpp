#include "utils/operations.h"

namespace wgpupixel {
using namespace detail;

void Commands::sharpen(const Image& source, const Image& destination,
                       const SharpenOptions& options) {
    Operation op(recording_.get(), "sharpen");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    auto src = op.resource(source.resource_, "source");
    auto dst = op.resource(destination.resource_, "destination");
    op.distinct(*src, *dst);
    op.same_size(*src, *dst);
    require_finite(op, options.strength, "strength");
    op.require(options.strength >= 0 && std::isfinite(1.0f + 4.0f * options.strength), "strength",
               "strength must be nonnegative with a finite center weight");
    auto record = kernel_record(Kernel::sharpen, src, dst);
    record.parameters.values = {options.strength, 1.0f + 4.0f * options.strength, 0, 0};
    op.append({record});
}
} // namespace wgpupixel
