#include "utils/operations.h"

namespace wgpupixel {
using namespace detail;

void Commands::upload(const UploadBuffer& source, const Image& destination,
                      const TransferOptions& options) {
    Operation op(recording_.get(), "upload");
    const auto& src = op.resource(source.resource_, "source", ResourceKind::upload);
    const auto& dst = op.resource(destination.resource_, "destination");
    op.require(src->element_bytes != 1, "source", "upload buffer must contain image pixels");
    const auto [x, y, width, height] = transfer_area(op, *dst, options.region);
    const auto pixels = std::uint64_t(width) * height;
    op.require(pixels <= src->capacity, "source", "upload buffer capacity is insufficient",
               ErrorCode::capacity);
    op.require(src->valid_bytes >= pixels * src->element_bytes, "source",
               "upload buffer does not contain enough initialized pixels");
    auto record = kernel_record(Kernel::upload, src, dst);
    // Buffer rectangle -> image: source dimensions are the rectangle, offsets its origin.
    record.parameters.dimensions = {width, height, dst->width, dst->height};
    record.parameters.dispatch = {0, 0, width, height};
    record.parameters.offsets = {std::int32_t(x), std::int32_t(y), 0, 0};
    record.parameters.reserved[0] = std::to_underlying(src->format);
    op.append({record});
}
} // namespace wgpupixel
