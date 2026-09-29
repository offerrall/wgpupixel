#include "utils/operations.h"
#include <limits>

namespace wgpupixel {
using namespace detail;

void Commands::checkerboard(const Image& destination, const CheckerboardOptions& options) {
    Operation op(recording_.get(), "checkerboard");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    auto pixels = op.resource(destination.resource_, "destination");
    op.require(options.size > 0 && options.size <= std::numeric_limits<std::int32_t>::max(), "size",
               "size must be a positive 32-bit signed integer");
    require_color(op, options.first, "first");
    require_color(op, options.second, "second");
    auto record = kernel_record(Kernel::checkerboard, pixels);
    record.parameters.color1 = rgba(options.first);
    record.parameters.color2 = rgba(options.second);
    record.parameters.reserved[0] = static_cast<std::uint32_t>(options.size);
    std::int64_t parity = 0;
    const std::int64_t offsets[] = {options.offset.x, options.offset.y};
    for (unsigned axis = 0; axis < 2; ++axis) {
        auto quotient = offsets[axis] / options.size;
        auto remainder = offsets[axis] % options.size;
        if (remainder < 0) {
            remainder += options.size;
            --quotient;
        }
        record.parameters.offsets[axis] = static_cast<std::int32_t>(remainder);
        parity += quotient;
    }
    record.parameters.reserved[1] = static_cast<std::uint32_t>((parity % 2 + 2) % 2);
    op.append({record});
}

} // namespace wgpupixel
