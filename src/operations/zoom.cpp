#include "utils/operations.h"
#include <limits>

namespace wgpupixel {
using namespace detail;

void Commands::zoom(const Image& source, const Image& destination, const ZoomOptions& options) {
    Operation op(recording_.get(), "zoom");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    auto src = op.resource(source.resource_, "source");
    auto dst = op.resource(destination.resource_, "destination");
    op.distinct(*src, *dst);
    require_finite(op, options.factor, "factor");
    op.require(options.factor > 0, "factor", "zoom factor must be positive");
    require_finite(op, options.center.x, "center");
    require_finite(op, options.center.y, "center");
    require_filter(op, options.filter);
    const float inverse = 1.0f / options.factor;
    op.require(std::isfinite(inverse), "factor", "reciprocal zoom factor must be finite");
    // Include rounding headroom for the shader's multiply and addition. The
    // half-dimension bound also covers rounding of large integer dimensions.
    const double limit = double(std::numeric_limits<float>::max()) * (1.0 - 1e-6);
    op.require(std::abs(double(options.center.x)) + double(dst->width) * 0.5 * inverse <= limit &&
                   std::abs(double(options.center.y)) + double(dst->height) * 0.5 * inverse <=
                       limit,
               "factor", "mapped coordinates exceed the finite sampling range");
    auto record = kernel_record(Kernel::zoom, src, dst);
    record.parameters.values = {inverse, options.center.x, options.center.y, 0};
    record.parameters.reserved[0] = std::to_underlying(options.filter);
    op.append({record});
}
} // namespace wgpupixel
