#include "geometry.h"
#include <numbers>
#include <limits>

namespace wgpupixel {
using namespace detail;
namespace {
MaskGeometry mask_geometry(Operation& op, const std::shared_ptr<Resource>& source,
                           const std::shared_ptr<Resource>& destination,
                           const std::shared_ptr<Resource>& selection, bool selected,
                           const std::optional<Rect>& region) {
    return {op, source, destination, selection, selected, region};
}
} // namespace

void Commands::resize(const Mask& source, const Mask& destination, const ResizeOptions& options) {
    Operation op(recording_.get(), "resize");
    const auto geometry = mask_geometry(op, source.resource_, destination.resource_,
                                        options.mask ? options.mask->resource_ : nullptr,
                                        options.mask != nullptr, options.region);
    require_geometry_filter(op, options.filter);
    const auto& dst = geometry.destination();
    const auto split = resize_split(extent(geometry.source()), extent(dst), options.filter);
    op.workspace(options.workspace.storage_,
                 split.split ? temporaries_plan({{split.width, split.height}}, op.name())
                             : WorkspacePlan{});
    if (options.filter == ResizeFilter::nearest) {
        geometry.append({geometry.record(MaskMapping::nearest_resize)});
        return;
    }
    // Same sampling as image resize: edge-clamped, kernels widened by the full
    // reduction, large reductions split into one pass per axis.
    std::vector<Record> records;
    auto mapping = MaskMapping::projection;
    std::shared_ptr<Resource> source_pixels = geometry.source_resource();
    if (split.split) {
        auto intermediate = op.temporary(split.width, split.height);
        auto pass = kernel_record(Kernel::resize_pass, source_pixels, intermediate);
        pass.parameters.reserved = {std::to_underlying(options.filter), 0, 1, 0};
        records.push_back(pass);
        source_pixels = intermediate;
        mapping = MaskMapping::float_projection;
    }
    auto record = geometry.record(mapping, EdgeMode::clamp, options.filter, source_pixels);
    set_projection(record, {{double(source_pixels->width) / dst.width, 0, 0, 0,
                             double(source_pixels->height) / dst.height, 0, 0, 0, 1}});
    record.parameters.values[3] = 1e30f; // No direct-sampling limit, as image resize.
    records.push_back(record);
    geometry.append(records);
}

void Commands::crop(const Mask& source, const Mask& destination, const CropOptions& options) {
    Operation op(recording_.get(), "crop");
    const auto geometry = mask_geometry(op, source.resource_, destination.resource_,
                                        options.mask ? options.mask->resource_ : nullptr,
                                        options.mask != nullptr, options.region);
    const auto& src = geometry.source();
    const auto& dst = geometry.destination();
    auto record = geometry.record(MaskMapping::integer);
    set_integer_mapping(
        record, {1, 1},
        {reduce_shift(options.origin.x, 1, src.width, dst.width, EdgeMode::transparent),
         reduce_shift(options.origin.y, 1, src.height, dst.height, EdgeMode::transparent)});
    geometry.append({record});
}

void Commands::flip(const Mask& source, const Mask& destination, const FlipOptions& options) {
    Operation op(recording_.get(), "flip");
    const auto geometry = mask_geometry(op, source.resource_, destination.resource_,
                                        options.mask ? options.mask->resource_ : nullptr,
                                        options.mask != nullptr, options.region);
    op.same_size(geometry.source(), geometry.destination());
    op.require(std::to_underlying(options.direction) <= std::to_underlying(FlipDirection::both),
               "direction", "flip direction is invalid");
    const bool horizontal = options.direction != FlipDirection::vertical;
    const bool vertical = options.direction != FlipDirection::horizontal;
    const auto& src = geometry.source();
    auto record = geometry.record(MaskMapping::integer);
    set_integer_mapping(record, {horizontal ? -1 : 1, vertical ? -1 : 1},
                        {horizontal ? std::int32_t(src.width - 1) : 0,
                         vertical ? std::int32_t(src.height - 1) : 0});
    geometry.append({record});
}

void Commands::rotate(const Mask& source, const Mask& destination, const RotateOptions& options) {
    Operation op(recording_.get(), "rotate");
    const auto geometry = mask_geometry(op, source.resource_, destination.resource_,
                                        options.mask ? options.mask->resource_ : nullptr,
                                        options.mask != nullptr, options.region);
    require_finite(op, options.degrees, "degrees");
    require_filter(op, options.filter);
    // The image rotate convention: clockwise about the centers of both masks.
    double angle = std::fmod(static_cast<double>(options.degrees), 360.0);
    if (angle < 0) {
        angle += 360.0;
    }
    double cosine = 1, sine = 0;
    if (angle == 90) {
        cosine = 0;
        sine = 1;
    } else if (angle == 180) {
        cosine = -1;
    } else if (angle == 270) {
        cosine = 0;
        sine = -1;
    } else if (angle != 0) {
        cosine = std::cos(angle * std::numbers::pi / 180.0);
        sine = std::sin(angle * std::numbers::pi / 180.0);
    }
    const auto& src = geometry.source();
    const auto& dst = geometry.destination();
    const double sx = src.width * 0.5, sy = src.height * 0.5;
    const double dx = dst.width * 0.5, dy = dst.height * 0.5;
    auto record = geometry.record(MaskMapping::projection, EdgeMode::transparent, options.filter);
    set_projection(record, {{cosine, sine, sx - cosine * dx - sine * dy, -sine, cosine,
                             sy + sine * dx - cosine * dy, 0, 0, 1}});
    geometry.append({record});
}
void Commands::zoom(const Mask& source, const Mask& destination, const ZoomOptions& options) {
    Operation op(recording_.get(), "zoom");
    const auto geometry = mask_geometry(op, source.resource_, destination.resource_,
                                        options.mask ? options.mask->resource_ : nullptr,
                                        options.mask != nullptr, options.region);
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
    op.require(std::abs(double(options.center.x)) +
                           double(geometry.destination().width) * 0.5 * inverse <=
                       limit &&
                   std::abs(double(options.center.y)) +
                           double(geometry.destination().height) * 0.5 * inverse <=
                       limit,
               "factor", "mapped coordinates exceed the finite sampling range");
    auto record = geometry.record(MaskMapping::zoom, EdgeMode::transparent, options.filter);
    record.parameters.values = {inverse, options.center.x, options.center.y, 0};
    geometry.append({record});
}
} // namespace wgpupixel
