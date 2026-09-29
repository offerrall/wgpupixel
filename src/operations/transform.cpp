#include "geometry.h"
#include <limits>

namespace wgpupixel {
using namespace detail;
namespace {
void check(bool condition, std::string_view operation, std::string_view parameter,
           std::string_view message) {
    if (!condition) {
        fail(ErrorCode::invalid_argument, operation, parameter, message);
    }
}

Homography affine_projection(std::string_view operation, const Affine& matrix) {
    const auto inverted = invert_affine(matrix);
    check(inverted.has_value(), operation, "matrix", "matrix must be finite and invertible");
    return *inverted;
}

Homography perspective_projection(std::string_view operation, Extent source,
                                  const std::array<Point, 4>& corners) {
    const auto forward = perspective_matrix({source.width, source.height}, corners);
    check(forward.has_value(), operation, "corners",
          "corners must be finite and form a strictly convex quadrilateral");
    const auto inverted = inverse(*forward);
    check(inverted.has_value(), operation, "corners", "perspective matrix is not invertible");
    return *inverted;
}

Homography projective_projection(std::string_view operation, Extent source, Homography matrix) {
    double scale = 0;
    for (double value : matrix.m) {
        check(std::isfinite(value), operation, "matrix", "matrix must be finite and invertible");
        scale = std::max(scale, std::abs(value));
    }
    check(scale > 0, operation, "matrix", "matrix must be invertible");
    // Normalize before inversion to avoid overflow and make H and -H equivalent.
    if (matrix.m[8] < 0) {
        scale = -scale;
    }
    for (auto& value : matrix.m) {
        value /= scale;
    }
    for (double x : {0.0, double(source.width)}) {
        for (double y : {0.0, double(source.height)}) {
            check(matrix.m[6] * x + matrix.m[7] * y + matrix.m[8] > 0, operation, "matrix",
                  "source rectangle must stay on one side of the horizon");
        }
    }
    auto result = inverse(matrix);
    check(result.has_value(), operation, "matrix", "matrix must be invertible");
    // Affine footprint calculations expect homogeneous w = 1.
    if (result->m[6] == 0 && result->m[7] == 0) {
        const double w = result->m[8];
        for (auto& value : result->m) {
            value /= w;
            check(std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max(),
                  operation, "matrix", "inverse matrix exceeds the finite sampling range");
        }
    }
    return *result;
}

Record image_record(Operation& op, const std::shared_ptr<Resource>& source,
                    const std::shared_ptr<Resource>& destination, ResizeFilter filter,
                    EdgeMode edge) {
    const auto& src = op.resource(source, "source");
    const auto& dst = op.resource(destination, "destination");
    op.distinct(*src, *dst);
    require_geometry_filter(op, filter);
    require_edge(op, edge);
    auto record = kernel_record(Kernel::transform, src, dst);
    record.parameters.reserved = {std::to_underlying(filter), std::to_underlying(edge), 0, 0};
    return record;
}

// The destination rectangle an image operation computes: its region, clipped.
std::array<std::int32_t, 4> image_work(Extent destination, const std::optional<Rect>& region) {
    const auto width = std::int64_t(destination.width), height = std::int64_t(destination.height);
    if (!region) {
        return {0, 0, std::int32_t(width), std::int32_t(height)};
    }
    return {
        std::int32_t(std::clamp<std::int64_t>(region->x, 0, width)),
        std::int32_t(std::clamp<std::int64_t>(region->y, 0, height)),
        std::int32_t(std::clamp<std::int64_t>(std::int64_t(region->x) + region->width, 0, width)),
        std::int32_t(
            std::clamp<std::int64_t>(std::int64_t(region->y) + region->height, 0, height))};
}

// Records of an image or mask projection following plan (see plan_projection).
// Large affine area reductions integrate exactly with a per-row table (binding 4);
// other affine reductions beyond the direct sampling limit first average the source
// (area); projective ones sample a box pyramid (binding 4) per pixel.
std::vector<Record> projection_records(Record record, const ProjectionPlan& plan, EdgeMode edge,
                                       const Temporary& temporary) {
    std::vector<Record> records;
    using Method = ProjectionPlan::Method;
    if (plan.method == Method::area_table) {
        record.sources[0] = build_area_table(records, record.source, temporary);
        record.parameters.reserved[3] = area_table_levels;
        set_projection(record, plan.projection);
        records.push_back(record);
        return records;
    }
    if (plan.method == Method::reduce) {
        record.source = reduce_area(records, record.source, plan.reduced, temporary);
        record.parameters.dimensions[0] = plan.reduced.width;
        record.parameters.dimensions[1] = plan.reduced.height;
        if (record.kernel == Kernel::mask_geometry) {
            record.parameters.reserved[2] = std::to_underlying(MaskMapping::float_projection);
        }
    } else if (plan.method == Method::pyramid) {
        record.sources[0] = build_pyramid(records, record.source, plan.levels, edge, temporary);
    }
    set_projection(record, plan.projection);
    if (plan.levels == 0 && record.source->kind == ResourceKind::image) {
        record.sources[0] = record.source;
    }
    record.parameters.reserved[3] = plan.levels;
    records.push_back(record);
    return records;
}

// Plans a projection of record.source into destination, checks the workspace
// against the plan and records it with workspace temporaries.
std::vector<Record> projection_records(Operation& op, const std::shared_ptr<WorkspaceStorage>& workspace,
                                       const Record& record, const Homography& projection,
                                       Extent destination, ResizeFilter filter, EdgeMode edge,
                                       std::array<std::int32_t, 4> work) {
    const auto plan = plan_projection(projection, extent(*record.source), destination, filter,
                                      edge, work, op.name());
    op.workspace(workspace, temporaries_plan(plan.temporaries, op.name()));
    return projection_records(record, plan, edge, [&](std::uint32_t width, std::uint32_t height) {
        return op.temporary(width, height);
    });
}
} // namespace

void Commands::transform(const Image& source, const Image& destination,
                         const TransformOptions& options) {
    Operation op(recording_.get(), "transform");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    auto record =
        image_record(op, source.resource_, destination.resource_, options.filter, options.edge);
    append_records(op, projection_records(op, options.workspace.storage_, record,
                                          affine_projection(op.name(), options.matrix),
                                          extent(*record.destination), options.filter, options.edge,
                                          image_work(extent(*record.destination), options.region)));
}

void Commands::transform(const Image& source, const Image& destination,
                         const ProjectiveTransformOptions& options) {
    Operation op(recording_.get(), "transform");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    auto record =
        image_record(op, source.resource_, destination.resource_, options.filter, options.edge);
    append_records(
        op, projection_records(op, options.workspace.storage_, record,
                               projective_projection(op.name(), extent(*record.source), options.matrix),
                               extent(*record.destination), options.filter, options.edge,
                               image_work(extent(*record.destination), options.region)));
}

void Commands::perspective(const Image& source, const Image& destination,
                           const PerspectiveOptions& options) {
    Operation op(recording_.get(), "perspective");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    auto record =
        image_record(op, source.resource_, destination.resource_, options.filter, options.edge);
    append_records(
        op, projection_records(op, options.workspace.storage_, record,
                               perspective_projection(op.name(), extent(*record.source), options.corners),
                               extent(*record.destination), options.filter, options.edge,
                               image_work(extent(*record.destination), options.region)));
}

void Commands::transform(const Mask& source, const Mask& destination,
                         const TransformOptions& options) {
    Operation op(recording_.get(), "transform");
    const MaskGeometry geometry(op, source.resource_, destination.resource_,
                                options.mask ? options.mask->resource_
                                             : std::shared_ptr<Resource>{},
                                options.mask != nullptr, options.region);
    require_geometry_filter(op, options.filter);
    require_edge(op, options.edge);
    const auto record = geometry.record(MaskMapping::projection, options.edge, options.filter);
    geometry.append(projection_records(op, options.workspace.storage_, record,
                                       affine_projection(op.name(), options.matrix),
                                       extent(geometry.destination()), options.filter, options.edge,
                                       record.parameters.offsets));
}

void Commands::transform(const Mask& source, const Mask& destination,
                         const ProjectiveTransformOptions& options) {
    Operation op(recording_.get(), "transform");
    const MaskGeometry geometry(op, source.resource_, destination.resource_,
                                options.mask ? options.mask->resource_
                                             : std::shared_ptr<Resource>{},
                                options.mask != nullptr, options.region);
    require_geometry_filter(op, options.filter);
    require_edge(op, options.edge);
    const auto record = geometry.record(MaskMapping::projection, options.edge, options.filter);
    geometry.append(projection_records(
        op, options.workspace.storage_, record,
        projective_projection(op.name(), extent(geometry.source()), options.matrix),
        extent(geometry.destination()), options.filter, options.edge, record.parameters.offsets));
}

void Commands::perspective(const Mask& source, const Mask& destination,
                           const PerspectiveOptions& options) {
    Operation op(recording_.get(), "perspective");
    const MaskGeometry geometry(op, source.resource_, destination.resource_,
                                options.mask ? options.mask->resource_
                                             : std::shared_ptr<Resource>{},
                                options.mask != nullptr, options.region);
    require_geometry_filter(op, options.filter);
    require_edge(op, options.edge);
    const auto record = geometry.record(MaskMapping::projection, options.edge, options.filter);
    geometry.append(projection_records(
        op, options.workspace.storage_, record,
        perspective_projection(op.name(), extent(geometry.source()), options.corners),
        extent(geometry.destination()), options.filter, options.edge, record.parameters.offsets));
}

namespace {
OperationRequirements projection_requirements(ImageSize source, ImageSize destination,
                                              ResizeFilter filter, EdgeMode edge,
                                              const std::optional<Rect>& region,
                                              std::string_view operation, auto projection) {
    check_geometry_sizes(source, destination, operation);
    check(std::to_underlying(filter) <= std::to_underlying(ResizeFilter::area), operation,
          "filter", "resize filter is invalid");
    check(std::to_underlying(edge) <= std::to_underlying(EdgeMode::mirror), operation, "edge",
          "edge mode is invalid");
    check(!region || (region->width >= 0 && region->height >= 0), operation, "region",
          "region dimensions must be nonnegative");
    const Extent from{std::uint32_t(source.width), std::uint32_t(source.height)};
    const Extent to{std::uint32_t(destination.width), std::uint32_t(destination.height)};
    const auto plan = plan_projection(projection(from), from, to, filter, edge,
                                      image_work(to, region), operation);
    return {destination, temporaries_plan(plan.temporaries, operation)};
}
} // namespace

OperationRequirements transform_requirements(ImageSize source, ImageSize destination,
                                             const TransformOptions& options) {
    constexpr std::string_view operation = "transform_requirements";
    return projection_requirements(source, destination, options.filter, options.edge,
                                   options.region, operation,
                                   [&](Extent) { return affine_projection(operation, options.matrix); });
}

OperationRequirements transform_requirements(ImageSize source, ImageSize destination,
                                             const ProjectiveTransformOptions& options) {
    constexpr std::string_view operation = "transform_requirements";
    return projection_requirements(
        source, destination, options.filter, options.edge, options.region, operation,
        [&](Extent from) { return projective_projection(operation, from, options.matrix); });
}

OperationRequirements perspective_requirements(ImageSize source, ImageSize destination,
                                               const PerspectiveOptions& options) {
    constexpr std::string_view operation = "perspective_requirements";
    return projection_requirements(
        source, destination, options.filter, options.edge, options.region, operation,
        [&](Extent from) { return perspective_projection(operation, from, options.corners); });
}
} // namespace wgpupixel
