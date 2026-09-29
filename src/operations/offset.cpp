#include "geometry.h"

namespace wgpupixel {
using namespace detail;

void Commands::offset(const Image& source, const Image& destination, const OffsetOptions& options) {
    Operation op(recording_.get(), "offset");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& src = op.resource(source.resource_, "source");
    const auto& dst = op.resource(destination.resource_, "destination");
    op.distinct(*src, *dst);
    require_edge(op, options.edge);
    auto record = kernel_record(Kernel::offset, src, dst);
    record.parameters.reserved[1] = std::to_underlying(options.edge);
    record.parameters.offsets[0] =
        reduce_shift(-std::int64_t(options.offset.x), 1, src->width, dst->width, options.edge);
    record.parameters.offsets[1] =
        reduce_shift(-std::int64_t(options.offset.y), 1, src->height, dst->height, options.edge);
    op.append({record});
}

void Commands::offset(const Mask& source, const Mask& destination, const OffsetOptions& options) {
    Operation op(recording_.get(), "offset");
    const MaskGeometry geometry(op, source.resource_, destination.resource_,
                                options.mask ? options.mask->resource_
                                             : std::shared_ptr<Resource>{},
                                options.mask != nullptr, options.region);
    require_edge(op, options.edge);
    const auto& src = geometry.source();
    const auto& dst = geometry.destination();
    auto record = geometry.record(MaskMapping::integer, options.edge);
    set_integer_mapping(
        record, {1, 1},
        {reduce_shift(-std::int64_t(options.offset.x), 1, src.width, dst.width, options.edge),
         reduce_shift(-std::int64_t(options.offset.y), 1, src.height, dst.height, options.edge)});
    geometry.append({record});
}
} // namespace wgpupixel
