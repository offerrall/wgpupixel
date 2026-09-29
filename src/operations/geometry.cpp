#include "geometry.h"
#include "../utils/workspace.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace wgpupixel {
namespace detail {

void set_projection(Record& record, const Homography& destination_to_source) {
    auto m = destination_to_source.m;
    // Affine matrices keep their exact entries; projective ones are normalized.
    const double scale =
        m[6] == 0 && m[7] == 0
            ? m[8]
            : *std::ranges::max_element(m, {}, [](double v) { return std::abs(v); });
    for (auto& value : m) {
        value /= std::abs(scale);
    }
    auto& p = record.parameters;
    p.values = {float(m[0]), float(m[1]), float(m[2]), 0};
    p.color1 = {float(m[3]), float(m[4]), float(m[5]), 0};
    p.color2 = {float(m[6]), float(m[7]), float(m[8]), 0};
}

std::optional<Homography> invert_affine(const Affine& t) {
    for (const float value : {t.a, t.b, t.c, t.d, t.e, t.f}) {
        if (!std::isfinite(value)) {
            return std::nullopt;
        }
    }
    const auto result = inverse(Homography::from(t));
    if (result) {
        for (const double value : result->m) {
            if (!(std::abs(value) <= std::numeric_limits<float>::max())) {
                return std::nullopt;
            }
        }
    }
    return result;
}

std::int32_t reduce_shift(std::int64_t shift, int sign, std::uint32_t source_extent,
                          std::uint32_t destination_extent, EdgeMode edge) {
    const auto source = std::int64_t(source_extent), destination = std::int64_t(destination_extent);
    if (edge == EdgeMode::repeat || edge == EdgeMode::mirror) {
        const auto period = edge == EdgeMode::repeat ? source : 2 * source;
        return static_cast<std::int32_t>(((shift % period) + period) % period);
    }
    // Beyond these bounds every destination pixel lands on the same side of the source.
    return static_cast<std::int32_t>(
        sign > 0 ? std::clamp(shift, -destination, source)
                 : std::clamp(shift, std::int64_t{-1}, source + destination - 1));
}

void set_integer_mapping(Record& record, std::array<std::int32_t, 2> sign,
                         std::array<std::int32_t, 2> shift) {
    record.parameters.extra = {std::bit_cast<float>(sign[0]), std::bit_cast<float>(sign[1]),
                               std::bit_cast<float>(shift[0]), std::bit_cast<float>(shift[1])};
}

MaskGeometry::MaskGeometry(Operation& op, const std::shared_ptr<Resource>& source,
                           const std::shared_ptr<Resource>& destination,
                           const std::shared_ptr<Resource>& selection, bool selected,
                           const std::optional<Rect>& region)
    : op_(op) {
    op.coverage(selection, destination, selected);
    if (region) {
        op.require(region->width >= 0 && region->height >= 0, "region",
                   "region dimensions must be nonnegative");
    }
    source_ = op.resource(source, "source", ResourceKind::mask);
    destination_ = op.resource(destination, "destination", ResourceKind::mask);
    op.distinct(*source_, *destination_);
    if (selected) {
        op.distinct(*selection, *destination_, "mask");
    }
    const auto width = std::int64_t(destination_->width),
               height = std::int64_t(destination_->height);
    std::int64_t left = 0, top = 0, right = width, bottom = height;
    if (region) {
        left = std::clamp<std::int64_t>(region->x, 0, width);
        top = std::clamp<std::int64_t>(region->y, 0, height);
        right = std::clamp<std::int64_t>(std::int64_t(region->x) + region->width, 0, width);
        bottom = std::clamp<std::int64_t>(std::int64_t(region->y) + region->height, 0, height);
    }
    region_ = {std::int32_t(left), std::int32_t(top), std::int32_t(right), std::int32_t(bottom)};
}

Record MaskGeometry::record(MaskMapping mapping, EdgeMode edge, ResizeFilter filter,
                            const std::shared_ptr<Resource>& source) const {
    auto record = kernel_record(Kernel::mask_geometry, source ? source : source_, destination_);
    auto& p = record.parameters;
    p.reserved = {std::to_underlying(filter), std::to_underlying(edge), std::to_underlying(mapping),
                  0};
    p.offsets = region_;
    // Packed coverage words laid out on a grid that fits the dispatch limits.
    const auto words = (std::uint64_t(destination_->width) * destination_->height + 3) / 4;
    const auto columns = std::min<std::uint64_t>(destination_->width, words);
    p.dispatch = {0, 0, static_cast<std::uint32_t>(columns),
                  static_cast<std::uint32_t>((words + columns - 1) / columns)};
    return record;
}

void MaskGeometry::append(std::vector<Record> records) const {
    if (region_[0] < region_[2] && region_[1] < region_[3]) {
        // Made after validation: the first mask geometry call of a context creates it.
        auto& state = *source_->owner.lock();
        for (auto& record : records) {
            if (record.kernel == Kernel::mask_geometry && !record.sources[0]) {
                if (!state.placeholder) {
                    state.placeholder = scratch_image(*source_, 1, 1, op_.name());
                }
                record.sources[0] = state.placeholder;
            }
        }
        append_records(op_, records);
    }
}

std::shared_ptr<Resource> scratch_image(const Resource& like, std::uint64_t width,
                                        std::uint64_t height, std::string_view operation) {
    auto state = like.owner.lock();
    const auto pixels = width * height;
    const auto& limits = state->limits;
    if (pixels == 0 || pixels > std::numeric_limits<std::uint32_t>::max() ||
        pixels * pixel_bytes > std::min(limits.maxBufferSize, limits.maxStorageBufferBindingSize)) {
        fail(ErrorCode::capacity, operation, "source",
             "intermediate image exceeds device buffer capacity");
    }
    auto resource = std::make_shared<Resource>();
    resource->owner = state;
    resource->kind = ResourceKind::image;
    resource->capacity = pixels;
    resource->width = static_cast<std::uint32_t>(width);
    resource->height = static_cast<std::uint32_t>(height);
    resource->buffer =
        allocate_buffer(*state, pixels * pixel_bytes, WGPUBufferUsage_Storage, operation);
    return resource;
}

namespace {
double footprint_limit(ResizeFilter filter, EdgeMode edge) {
    // footprint_taps(_extended) / 2 in footprint.wgsl.
    constexpr std::array radius{0.5, 1.0, 2.0, 3.0, 0.5};
    if (edge != EdgeMode::transparent && filter == ResizeFilter::area) {
        return 6;
    }
    return (edge == EdgeMode::transparent ? 32.0 : 12.0) / radius[std::to_underlying(filter)];
}
} // namespace

std::uint32_t pyramid_levels(const Homography& g, Extent source, Extent destination,
                             ResizeFilter filter, EdgeMode edge) {
    if (filter == ResizeFilter::nearest) {
        return 0;
    }
    std::uint32_t depth = 0;
    for (std::uint64_t w = source.width, h = source.height; w > 1 || h > 1; ++depth) {
        w = (w + 1) / 2;
        h = (h + 1) / 2;
    }
    const auto& m = g.m;
    // Largest principal length of the footprint at a destination point; empty
    // beyond the horizon.
    const auto largest = [&](double x, double y) -> std::optional<double> {
        const double w = m[6] * x + m[7] * y + m[8];
        if (!(w > 0)) {
            return std::nullopt;
        }
        const double qx = (m[0] * x + m[1] * y + m[2]) / w;
        const double qy = (m[3] * x + m[4] * y + m[5]) / w;
        const double a = (m[0] - qx * m[6]) / w, b = (m[3] - qy * m[6]) / w;
        const double c = (m[1] - qx * m[7]) / w, d = (m[4] - qy * m[7]) / w;
        const double p = a * a + c * c, r = b * b + d * d, k = a * b + c * d;
        return std::sqrt(0.5 * (p + r) + std::sqrt(0.25 * (p - r) * (p - r) + k * k));
    };
    double sigma = 0;
    std::uint32_t margin = 0;
    if (m[6] == 0 && m[7] == 0) {
        sigma = largest(0, 0).value_or(0);
    } else {
        // Projective footprints vary; sample the destination and keep a level of margin.
        // A horizon inside the destination needs every level.
        constexpr int steps = 32;
        for (int j = 0; j <= steps; ++j) {
            for (int i = 0; i <= steps; ++i) {
                const auto value = largest(0.5 + (destination.width - 1.0) * i / steps,
                                           0.5 + (destination.height - 1.0) * j / steps);
                if (!value) {
                    return depth;
                }
                sigma = std::max(sigma, *value);
            }
        }
        margin = 1;
    }
    const double ratio = sigma / footprint_limit(filter, edge);
    if (!(ratio > 1)) {
        return 0;
    }
    if (!(ratio < 1e9)) {
        return depth;
    }
    return std::min(depth, std::uint32_t(std::ceil(std::log2(ratio))) + margin);
}

std::shared_ptr<Resource> build_pyramid(std::vector<Record>& records,
                                        const std::shared_ptr<Resource>& source,
                                        std::uint32_t levels, EdgeMode edge,
                                        const Temporary& temporary) {
    std::uint64_t total = 0;
    std::vector<std::array<std::uint32_t, 2>> sizes;
    for (std::uint32_t w = source->width, h = source->height, k = 1; k <= levels; ++k) {
        w = (w + 1) / 2;
        h = (h + 1) / 2;
        sizes.push_back({w, h});
        total += std::uint64_t(w) * h;
    }
    auto pyramid = temporary(static_cast<std::uint32_t>(total), 1);
    for (std::uint32_t k = 1; k <= levels; ++k) {
        auto record = kernel_record(Kernel::geometry_pyramid, source, pyramid);
        auto& p = record.parameters;
        p.reserved = {k, source->kind == ResourceKind::mask ? 1u : 0u, std::to_underlying(edge), 0};
        p.dispatch = {0, 0, sizes[k - 1][0], sizes[k - 1][1]};
        records.push_back(record);
    }
    return pyramid;
}

ResizeSplit resize_split(Extent source, Extent destination, ResizeFilter filter) {
    constexpr std::array radius{0.5, 1.0, 2.0, 3.0, 0.5};
    const double r = radius[std::to_underlying(filter)];
    const double sx = double(source.width) / destination.width;
    const double sy = double(source.height) / destination.height;
    const double taps = (2 * r * std::max(sx, 1.0) + 2) * (2 * r * std::max(sy, 1.0) + 2);
    if (filter == ResizeFilter::nearest || taps <= 4096) {
        return {};
    }
    return sx >= sy ? ResizeSplit{true, destination.width, source.height}
                    : ResizeSplit{true, source.width, destination.height};
}

namespace {
double affine_footprint(const Homography& g) {
    const auto& m = g.m;
    const double p = m[0] * m[0] + m[1] * m[1], r = m[3] * m[3] + m[4] * m[4];
    const double k = m[0] * m[3] + m[1] * m[4];
    return std::sqrt(0.5 * (p + r) + std::sqrt(0.25 * (p - r) * (p - r) + k * k));
}
} // namespace

bool use_area_table(const Homography& g, std::uint64_t work_pixels, EdgeMode edge,
                    ResizeFilter filter) {
    if (filter != ResizeFilter::area) {
        return false;
    }
    const double largest = affine_footprint(g);
    if (!(largest > footprint_limit(filter, edge))) {
        return false;
    }
    if (edge == EdgeMode::transparent || edge == EdgeMode::clamp) {
        return true;
    }
    // Every computed pixel of a tiled (repeat, mirror) reduction integrates about
    // `largest` source rows per footprint edge, each summing up to largest / 64 table
    // chunks. About 2^24 of these units take a quarter of a second on a desktop GPU.
    const double units = double(work_pixels) * largest * (4 + std::min(largest, 8192.0) / 64);
    return units <= double(1 << 24);
}

namespace {
// Area table layout: one prefix row per source row plus one partial sum per
// 4096-pixel group.
std::uint64_t area_table_pixels(Extent source) {
    const std::uint64_t width = source.width, groups = (width + 4095) / 4096;
    return (width + groups) * source.height;
}
std::uint64_t pyramid_pixels(Extent source, std::uint32_t levels) {
    std::uint64_t total = 0;
    for (std::uint32_t w = source.width, h = source.height, k = 1; k <= levels; ++k) {
        w = (w + 1) / 2;
        h = (h + 1) / 2;
        total += std::uint64_t(w) * h;
    }
    return total;
}
} // namespace

std::shared_ptr<Resource> build_area_table(std::vector<Record>& records,
                                           const std::shared_ptr<Resource>& source,
                                           const Temporary& temporary) {
    const std::uint64_t width = source->width, height = source->height;
    const std::uint64_t groups = (width + 4095) / 4096;
    auto table = temporary(static_cast<std::uint32_t>(area_table_pixels(extent(*source))), 1);
    for (std::uint32_t pass = 0; pass < 2; ++pass) {
        auto record = kernel_record(Kernel::area_table, source, table);
        record.parameters.reserved = {pass, source->kind == ResourceKind::mask ? 1u : 0u, 0, 0};
        record.parameters.dispatch = {
            0, 0, static_cast<std::uint32_t>(pass == 0 ? (width + 63) / 64 : groups),
            static_cast<std::uint32_t>(height)};
        records.push_back(record);
    }
    return table;
}

Extent reduced_size(const Homography& g, Extent source, ResizeFilter filter, EdgeMode edge) {
    const Extent same = source;
    if (filter == ResizeFilter::nearest) {
        return same;
    }
    const auto& m = g.m;
    const double p = m[0] * m[0] + m[1] * m[1], r = m[3] * m[3] + m[4] * m[4];
    const double k = m[0] * m[3] + m[1] * m[4];
    const double largest = std::sqrt(0.5 * (p + r) + std::sqrt(0.25 * (p - r) * (p - r) + k * k));
    const double limit = footprint_limit(filter, edge);
    if (!(largest > limit)) {
        return same;
    }
    // Shrink each axis until the footprint's extent along it is at most half the limit.
    const auto axis = [&](double extent, std::uint32_t size) {
        const double factor = std::max(1.0, extent / (0.5 * limit));
        return static_cast<std::uint32_t>(std::max(1.0, std::ceil(size / factor)));
    };
    return {axis(std::abs(m[0]) + std::abs(m[1]), source.width),
            axis(std::abs(m[3]) + std::abs(m[4]), source.height)};
}

std::shared_ptr<Resource> reduce_area(std::vector<Record>& records,
                                      const std::shared_ptr<Resource>& source, Extent size,
                                      const Temporary& temporary) {
    const std::uint32_t mask = source->kind == ResourceKind::mask ? 1 : 0;
    auto result = temporary(size.width, size.height);
    auto from = source;
    std::uint32_t kind = mask;
    if (const auto split = resize_split(extent(*source), size, ResizeFilter::area); split.split) {
        auto intermediate = temporary(split.width, split.height);
        auto pass = kernel_record(Kernel::resize_pass, source, intermediate);
        pass.parameters.reserved = {std::to_underlying(ResizeFilter::area), 0, mask, 0};
        records.push_back(pass);
        from = intermediate;
        kind = 0;
    }
    auto pass = kernel_record(Kernel::resize_pass, from, result);
    pass.parameters.reserved = {std::to_underlying(ResizeFilter::area), 0, kind, 0};
    records.push_back(pass);
    return result;
}

void append_records(Operation& op, const std::vector<Record>& records) {
    op.reserve(records.size());
    for (const auto& record : records) {
        op.append({record});
    }
}

WorkspacePlan temporaries_plan(const std::vector<Extent>& temporaries, std::string_view operation) {
    WorkspacePlan plan;
    for (const auto& size : temporaries) {
        workspace_buffer(plan, std::uint64_t(size.width) * size.height * pixel_bytes, operation);
    }
    return plan;
}

ProjectionPlan plan_projection(const Homography& g, Extent source, Extent destination,
                               ResizeFilter filter, EdgeMode edge,
                               std::array<std::int32_t, 4> work, std::string_view operation) {
    ProjectionPlan plan;
    plan.projection = g;
    const auto temporary = [&](std::uint64_t width, std::uint64_t height) {
        if (width * height > std::numeric_limits<std::uint32_t>::max()) {
            fail(ErrorCode::capacity, operation, "source",
                 "intermediate image exceeds shader address capacity");
        }
        plan.temporaries.push_back({std::uint32_t(width), std::uint32_t(height)});
    };
    const auto& m = g.m;
    const bool affine = m[6] == 0 && m[7] == 0;
    const auto work_pixels = std::uint64_t(std::max(0, work[2] - work[0])) *
                             std::uint64_t(std::max(0, work[3] - work[1]));
    if (affine && use_area_table(g, work_pixels, edge, filter)) {
        plan.method = ProjectionPlan::Method::area_table;
        temporary(area_table_pixels(source), 1);
    } else if (affine) {
        const auto reduced = reduced_size(g, source, filter, edge);
        if (reduced.width != source.width || reduced.height != source.height) {
            plan.method = ProjectionPlan::Method::reduce;
            plan.reduced = reduced;
            const double sx = double(reduced.width) / source.width;
            const double sy = double(reduced.height) / source.height;
            for (int i = 0; i < 3; ++i) {
                plan.projection.m[i] *= sx;
                plan.projection.m[3 + i] *= sy;
            }
            temporary(reduced.width, reduced.height);
            if (const auto split = resize_split(source, reduced, ResizeFilter::area); split.split) {
                temporary(split.width, split.height);
            }
        }
    } else {
        plan.levels = pyramid_levels(g, source, destination, filter, edge);
        if (plan.levels > 0) {
            plan.method = ProjectionPlan::Method::pyramid;
            temporary(pyramid_pixels(source, plan.levels), 1);
        }
    }
    return plan;
}

} // namespace detail

namespace {
constexpr auto int_max = double(std::numeric_limits<std::int32_t>::max());

Rect bounds(const Rect& source, std::string_view parameter, auto map) {
    if (source.width <= 0 || source.height <= 0) {
        detail::fail(ErrorCode::invalid_argument, "transform_bounds", "source",
                     "source rectangle must be nonempty");
    }
    double left = INFINITY, top = INFINITY, right = -INFINITY, bottom = -INFINITY;
    const double x0 = source.x, y0 = source.y;
    const double x1 = x0 + source.width, y1 = y0 + source.height;
    for (const auto& [x, y] : {std::pair{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}) {
        const auto [px, py] = map(x, y);
        left = std::min(left, px);
        top = std::min(top, py);
        right = std::max(right, px);
        bottom = std::max(bottom, py);
    }
    const auto snap = [](double value, auto round) {
        const double nearest = std::round(value);
        return std::abs(value - nearest) <= 1e-3 ? nearest : round(value);
    };
    left = snap(left, [](double v) { return std::floor(v); });
    top = snap(top, [](double v) { return std::floor(v); });
    right = snap(right, [](double v) { return std::ceil(v); });
    bottom = snap(bottom, [](double v) { return std::ceil(v); });
    if (!(left >= -int_max - 1 && top >= -int_max - 1 && right <= int_max && bottom <= int_max &&
          right - left <= int_max && bottom - top <= int_max)) {
        detail::fail(ErrorCode::invalid_argument, "transform_bounds", parameter,
                     "transformed bounds must be finite and fit int32");
    }
    return {std::int32_t(left), std::int32_t(top), std::int32_t(right - left),
            std::int32_t(bottom - top)};
}
} // namespace

std::optional<Homography> perspective_matrix(ImageSize source,
                                             const std::array<Point, 4>& corners) {
    if (source.width <= 0 || source.height <= 0) {
        return std::nullopt;
    }
    std::array<double, 4> x{}, y{};
    for (int i = 0; i < 4; ++i) {
        if (!std::isfinite(corners[i].x) || !std::isfinite(corners[i].y)) {
            return std::nullopt;
        }
        x[i] = corners[i].x;
        y[i] = corners[i].y;
    }
    // Strict convexity: consecutive edges turn the same way, and not negligibly.
    int orientation = 0;
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4, k = (i + 2) % 4;
        const double ax = x[j] - x[i], ay = y[j] - y[i], bx = x[k] - x[j], by = y[k] - y[j];
        const double cross = ax * by - ay * bx;
        if (!(std::abs(cross) > 1e-9 * std::hypot(ax, ay) * std::hypot(bx, by))) {
            return std::nullopt;
        }
        const int sign = cross > 0 ? 1 : -1;
        if (orientation != 0 && sign != orientation) {
            return std::nullopt;
        }
        orientation = sign;
    }
    // Unit square to quadrilateral (Heckbert), then source rectangle to unit square.
    const double sx = x[0] - x[1] + x[2] - x[3], sy = y[0] - y[1] + y[2] - y[3];
    const double dx1 = x[1] - x[2], dx2 = x[3] - x[2], dy1 = y[1] - y[2], dy2 = y[3] - y[2];
    const double den = dx1 * dy2 - dx2 * dy1;
    const double g = (sx * dy2 - dx2 * sy) / den, h = (dx1 * sy - sx * dy1) / den;
    const double w = double(source.width), v = double(source.height);
    Homography result{{(x[1] - x[0] + g * x[1]) / w, (x[3] - x[0] + h * x[3]) / v, x[0],
                       (y[1] - y[0] + g * y[1]) / w, (y[3] - y[0] + h * y[3]) / v, y[0], g / w,
                       h / v, 1}};
    for (const double value : result.m) {
        if (!std::isfinite(value)) {
            return std::nullopt;
        }
    }
    // Convexity keeps the whole rectangle in front of the horizon (w > 0).
    if (!(1 + g > 0 && 1 + h > 0 && 1 + g + h > 0)) {
        return std::nullopt;
    }
    return result;
}

Rect transform_bounds(const Rect& source, const Affine& matrix) {
    for (const float value : {matrix.a, matrix.b, matrix.c, matrix.d, matrix.e, matrix.f}) {
        if (!std::isfinite(value)) {
            detail::fail(ErrorCode::invalid_argument, "transform_bounds", "matrix",
                         "matrix must be finite");
        }
    }
    return bounds(source, "matrix", [&](double x, double y) {
        return std::pair{matrix.a * x + matrix.c * y + matrix.e,
                         matrix.b * x + matrix.d * y + matrix.f};
    });
}

Rect transform_bounds(const Rect& source, const Homography& matrix) {
    const auto& m = matrix.m;
    // H and -H describe the same mapping; orient w by the rectangle's first corner.
    const double orientation =
        m[6] * double(source.x) + m[7] * double(source.y) + m[8] < 0 ? -1.0 : 1.0;
    return bounds(source, "matrix", [&](double x, double y) {
        const double w = (m[6] * x + m[7] * y + m[8]) * orientation;
        if (!(w > 0) || !std::isfinite(w)) {
            detail::fail(ErrorCode::invalid_argument, "transform_bounds", "matrix",
                         "rectangle must lie in front of the projection horizon");
        }
        return std::pair{(m[0] * x + m[1] * y + m[2]) * orientation / w,
                         (m[3] * x + m[4] * y + m[5]) * orientation / w};
    });
}
} // namespace wgpupixel
