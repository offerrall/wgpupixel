#include "utils/operations.h"

namespace wgpupixel {
using namespace detail;

void Commands::chroma_key(const Image& destination, const ChromaKeyOptions& options) {
    Operation op(recording_.get(), "chroma_key");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    auto pixels = op.resource(destination.resource_, "destination");
    require_color(op, options.key, "key");
    op.require(options.key.a > 0, "key", "key alpha must be positive");
    const Color straight{options.key.r / options.key.a, options.key.g / options.key.a,
                         options.key.b / options.key.a, 1};
    op.require(std::isfinite(straight.r) && std::isfinite(straight.g) &&
                   std::isfinite(straight.b) && straight.r >= 0 && straight.g >= 0 &&
                   straight.b >= 0,
               "key", "straight key RGB must be finite and nonnegative");
    require_finite(op, options.threshold, "threshold");
    op.require(options.threshold >= 0, "threshold", "threshold must be nonnegative");
    require_finite(op, options.smoothness, "smoothness");
    op.require(options.smoothness >= 0, "smoothness", "smoothness must be nonnegative");
    require_finite(op, options.spill_suppression, "spill_suppression");
    op.require(options.spill_suppression >= 0 && options.spill_suppression <= 1,
               "spill_suppression", "spill suppression must be between zero and one");
    auto record = kernel_record(Kernel::chroma_key, pixels);
    record.parameters.color1 = rgba(straight);
    record.parameters.values = {options.threshold, options.smoothness, options.spill_suppression,
                                0};
    op.append({record});
}
} // namespace wgpupixel
