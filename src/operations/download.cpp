#include "utils/operations.h"

namespace wgpupixel {
using namespace detail;

void Commands::download(const Image& source, const ReadbackBuffer& destination,
                        const TransferOptions& options) {
    Operation op(recording_.get(), "download");
    const auto& src = op.resource(source.resource_, "source");
    const auto& dst = op.resource(destination.resource_, "destination", ResourceKind::readback);
    op.require(dst->element_bytes != 1, "destination",
               "readback buffer must contain image pixels");
    const auto [x, y, width, height] = transfer_area(op, *src, options.region);
    const auto pixels = std::uint64_t(width) * height;
    op.require(pixels <= dst->capacity, "destination", "readback buffer capacity is insufficient",
               ErrorCode::capacity);
    auto record = kernel_record(Kernel::download, src, dst);
    // Image rectangle -> buffer: destination dimensions are the rectangle.
    record.parameters.dimensions = {src->width, src->height, width, height};
    record.parameters.dispatch = {0, 0, width, height};
    record.parameters.offsets = {std::int32_t(x), std::int32_t(y), 0, 0};
    record.parameters.reserved[0] = std::to_underlying(dst->format);
    record.bytes = pixels * dst->element_bytes;
    op.append({record});
}
} // namespace wgpupixel
