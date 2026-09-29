#include "selection.h"

namespace wgpupixel {
using namespace detail;

void Commands::upload(const UploadBuffer& source, const Mask& destination,
                      const TransferOptions& options) {
    Operation op(recording_.get(), "upload");
    const auto& src = op.resource(source.resource_, "source", ResourceKind::upload);
    const auto& dst = op.resource(destination.resource_, "destination", ResourceKind::mask);
    op.require(src->element_bytes == 1, "source", "upload buffer must contain mask coverage");
    const auto [x, y, width, height] = transfer_area(op, *dst, options.region);
    const auto pixels = std::uint64_t(width) * height;
    op.require(src->capacity >= pixels, "source", "upload capacity is insufficient",
               ErrorCode::capacity);
    op.require(src->valid_bytes >= pixels, "source", "upload buffer lacks initialized coverage");
    auto record = kernel_record(Kernel::mask_upload, src, dst);
    record.parameters.dimensions = {width, height, dst->width, dst->height};
    record.parameters.offsets = {std::int32_t(x), std::int32_t(y), 0, 0};
    record.parameters.dispatch = linear_dispatch(mask_words(*dst));
    record.bytes = pixels;
    op.append({record});
}
void Commands::download(const Mask& source, const ReadbackBuffer& destination,
                        const TransferOptions& options) {
    Operation op(recording_.get(), "download");
    const auto& src = op.resource(source.resource_, "source", ResourceKind::mask);
    const auto& dst = op.resource(destination.resource_, "destination", ResourceKind::readback);
    op.require(dst->element_bytes == 1, "destination",
               "readback buffer must contain mask coverage");
    const auto [x, y, width, height] = transfer_area(op, *src, options.region);
    const auto pixels = std::uint64_t(width) * height;
    op.require(dst->capacity >= pixels, "destination", "readback capacity is insufficient",
               ErrorCode::capacity);
    auto record = kernel_record(Kernel::mask_download, src, dst);
    record.parameters.dimensions = {src->width, src->height, width, height};
    record.parameters.offsets = {std::int32_t(x), std::int32_t(y), 0, 0};
    record.parameters.dispatch = linear_dispatch((pixels + 3) / 4);
    record.bytes = pixels;
    op.append({record});
}
void Commands::copy(const Mask& source, const Mask& destination, const CopyOptions& options) {

    Operation op(recording_.get(), "copy");
    const auto& src = op.resource(source.resource_, "source", ResourceKind::mask);
    const auto& dst = op.resource(destination.resource_, "destination", ResourceKind::mask);
    op.distinct(*src, *dst);
    op.same_size(*src, *dst);
    auto record = kernel_record(Kernel::mask_combine, src, dst);
    mask_write(op, record,
               options.mask ? op.resource(options.mask->resource_, "mask", ResourceKind::mask)
                            : nullptr,
               options.region);
    op.append({record});
}
void Commands::fill(const Mask& mask, const MaskFillOptions& options) {
    Operation op(recording_.get(), "fill");
    const auto& target = op.resource(mask.resource_, "mask", ResourceKind::mask);
    require_finite(op, options.coverage, "coverage");
    op.require(options.coverage >= 0 && options.coverage <= 1, "coverage",
               "coverage must lie in [0, 1]");
    auto record = kernel_record(Kernel::mask_fill, target);
    record.parameters.reserved[0] =
        static_cast<std::uint32_t>(std::floor(double(options.coverage) * 255 + 0.5));
    mask_write(op, record,
               options.mask ? op.resource(options.mask->resource_, "mask", ResourceKind::mask)
                            : nullptr,
               options.region);
    op.append({record});
}
void Commands::invert(const Mask& destination, const InvertOptions& options) {

    Operation op(recording_.get(), "invert");
    const auto& target = op.resource(destination.resource_, "destination", ResourceKind::mask);
    auto record = kernel_record(Kernel::mask_invert, target);
    mask_write(op, record,
               options.mask ? op.resource(options.mask->resource_, "mask", ResourceKind::mask)
                            : nullptr,
               options.region);
    op.append({record});
}
void Commands::extract_mask(const Image& source, const Mask& destination,
                            const ExtractMaskOptions& options) {
    Operation op(recording_.get(), "extract_mask");
    const auto& src = op.resource(source.resource_, "source");
    const auto& dst = op.resource(destination.resource_, "destination", ResourceKind::mask);
    op.same_size(*src, *dst);
    op.require(std::to_underlying(options.mode) <= std::to_underlying(MaskMode::luminance), "mode",
               "unknown mask mode");
    auto record = kernel_record(Kernel::mask_extract, src, dst);
    record.parameters.reserved[0] = std::to_underlying(options.mode);
    op.append({record});
}
} // namespace wgpupixel
