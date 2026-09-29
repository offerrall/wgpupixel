#include "geometry.h"
#include <limits>

namespace wgpupixel {
using namespace detail;

void Commands::resize(const Image& source, const Image& destination, const ResizeOptions& options) {
    Operation op(recording_.get(), "resize");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& src = op.resource(source.resource_, "source");
    const auto& dst = op.resource(destination.resource_, "destination");
    op.distinct(*src, *dst);
    op.require(std::to_underlying(options.filter) <= std::to_underlying(ResizeFilter::area),
               "filter", "resize filter is invalid");
    const auto split = resize_split(extent(*src), extent(*dst), options.filter);
    op.workspace(options.workspace.storage_,
                 split.split ? temporaries_plan({{split.width, split.height}}, op.name())
                             : WorkspacePlan{});
    if (!split.split) {
        auto record = kernel_record(Kernel::resize, src, dst);
        record.parameters.reserved[0] = std::to_underlying(options.filter);
        op.append({record});
        return;
    }
    // Large reductions: one pass per axis, so each invocation reads one line of taps.
    auto intermediate = op.temporary(split.width, split.height);
    auto first = kernel_record(Kernel::resize_pass, src, intermediate);
    first.parameters.reserved[0] = std::to_underlying(options.filter);
    auto second = kernel_record(Kernel::resize, intermediate, dst);
    second.parameters.reserved[0] = std::to_underlying(options.filter);
    op.append({first, second});
}

namespace {
void check_size(ImageSize size, std::string_view operation, std::string_view parameter) {
    if (!(size.width > 0 && size.height > 0 && size.width <= std::numeric_limits<std::int32_t>::max() &&
          size.height <= std::numeric_limits<std::int32_t>::max())) {
        fail(ErrorCode::invalid_argument, operation, parameter, "dimensions must be positive int32");
    }
    if (std::uint64_t(size.width) * std::uint64_t(size.height) > UINT32_MAX) {
        fail(ErrorCode::capacity, operation, parameter, "pixel count exceeds shader address capacity");
    }
}
} // namespace

namespace detail {
void check_geometry_sizes(ImageSize source, ImageSize destination, std::string_view operation) {
    check_size(source, operation, "source");
    check_size(destination, operation, "destination");
}
} // namespace detail

OperationRequirements resize_requirements(ImageSize source, ImageSize destination,
                                          const ResizeOptions& options) {
    constexpr std::string_view operation = "resize_requirements";
    check_geometry_sizes(source, destination, operation);
    if (std::to_underlying(options.filter) > std::to_underlying(ResizeFilter::area)) {
        fail(ErrorCode::invalid_argument, operation, "filter", "resize filter is invalid");
    }
    const Extent from{std::uint32_t(source.width), std::uint32_t(source.height)};
    const Extent to{std::uint32_t(destination.width), std::uint32_t(destination.height)};
    const auto split = resize_split(from, to, options.filter);
    return {destination, split.split ? temporaries_plan({{split.width, split.height}}, operation)
                                     : WorkspacePlan{}};
}
} // namespace wgpupixel
