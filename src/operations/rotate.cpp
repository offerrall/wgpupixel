#include "utils/operations.h"
#include <numbers>

namespace wgpupixel {
using namespace detail;

void Commands::rotate(const Image& source, const Image& destination, const RotateOptions& options) {
    Operation op(recording_.get(), "rotate");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    auto src = op.resource(source.resource_, "source");
    auto dst = op.resource(destination.resource_, "destination");
    op.distinct(*src, *dst);
    require_finite(op, options.degrees, "degrees");
    require_filter(op, options.filter);
    double angle = std::fmod(static_cast<double>(options.degrees), 360.0);
    if (angle < 0) {
        angle += 360.0;
    }
    float cosine = 1, sine = 0;
    if (angle == 90) {
        cosine = 0;
        sine = 1;
    } else if (angle == 180) {
        cosine = -1;
        sine = 0;
    } else if (angle == 270) {
        cosine = 0;
        sine = -1;
    } else if (angle != 0) {
        const double radians = angle * std::numbers::pi / 180.0;
        cosine = static_cast<float>(std::cos(radians));
        sine = static_cast<float>(std::sin(radians));
    }
    auto record = kernel_record(Kernel::rotate, src, dst);
    record.parameters.values = {cosine, sine, 0, 0};
    record.parameters.reserved[0] = std::to_underlying(options.filter);
    op.append({record});
}
} // namespace wgpupixel
