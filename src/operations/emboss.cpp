#include "utils/operations.h"

namespace wgpupixel {
using namespace detail;

void Commands::emboss(const Image& source, const Image& destination, const EmbossOptions& options) {
    Operation op(recording_.get(), "emboss");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    auto src = op.resource(source.resource_, "source");
    auto dst = op.resource(destination.resource_, "destination");
    op.distinct(*src, *dst);
    op.same_size(*src, *dst);
    require_finite(op, options.strength, "strength");
    op.require(options.strength >= 0 && std::isfinite(2.0f * options.strength), "strength",
               "strength must be nonnegative with finite filter coefficients");
    auto record = kernel_record(Kernel::emboss, src, dst);
    record.parameters.values[0] = options.strength;
    op.append({record});
}
} // namespace wgpupixel
