#include "selection.h"
#include <limits>
#include <numbers>

namespace wgpupixel {
using namespace detail;
namespace {
// Curves are flattened in destination space to within 1/64 pixel. Vertices sit
// slightly outside the curve so chords straddle it instead of shrinking the area.
constexpr double flatness = 1.0 / 64;

struct Path {
    Affine transform;
    std::vector<std::array<double, 4>> edges;

    std::array<double, 2> map(double x, double y) const {
        return {transform.a * x + transform.c * y + transform.e,
                transform.b * x + transform.d * y + transform.f};
    }
    void line(std::array<double, 2> from, std::array<double, 2> to) {
        if (from != to) {
            edges.push_back({from[0], from[1], to[0], to[1]});
        }
    }
    // A closed contour of mapped points. Runs of points within 1/1024 pixel of the chord
    // joining their ends (dense lasso samples) become that chord, which changes coverage
    // by less than one 8-bit step while bounding the work per pixel of outline.
    void contour(std::span<const std::array<double, 2>> points) {
        constexpr double tolerance = 1.0 / 1024;
        constexpr std::size_t longest = 256;
        const std::size_t count = points.size();
        if (count < 2) {
            return;
        }
        std::size_t anchor = 0;
        while (anchor < count) {
            // Skip following points while they stay within tolerance of the chord from
            // the anchor to the next candidate (index count closes back to point 0).
            std::size_t end = anchor + 1;
            while (end < count && end - anchor < longest) {
                const auto& a = points[anchor];
                const auto& b = points[(end + 1) % count];
                const double dx = b[0] - a[0], dy = b[1] - a[1];
                const double squared = dx * dx + dy * dy;
                bool close = true;
                for (std::size_t k = anchor + 1; close && k <= end; ++k) {
                    const double px = points[k][0] - a[0], py = points[k][1] - a[1];
                    const double t =
                        squared > 0 ? std::clamp((px * dx + py * dy) / squared, 0.0, 1.0) : 0.0;
                    close = std::hypot(px - t * dx, py - t * dy) <= tolerance;
                }
                if (!close) {
                    break;
                }
                ++end;
            }
            line(points[anchor], points[end % count]);
            anchor = end;
        }
    }
    void polygon(std::span<const std::array<double, 2>> points) {
        for (std::size_t i = 0; i < points.size(); ++i) {
            line(map(points[i][0], points[i][1]),
                 map(points[(i + 1) % points.size()][0], points[(i + 1) % points.size()][1]));
        }
    }
    double scale() const {
        return std::sqrt(double(transform.a) * transform.a + double(transform.b) * transform.b +
                         double(transform.c) * transform.c + double(transform.d) * transform.d);
    }
    // Segment count for a full turn of the given radius, and the vertex radius factor.
    std::pair<std::size_t, double> turn(double radius) const {
        const double extent = radius * scale();
        std::size_t segments = 8;
        if (extent > flatness) {
            segments = std::clamp<std::size_t>(
                std::size_t(std::ceil(std::numbers::pi / std::acos(1 - flatness / extent))), 8,
                16384);
        }
        return {segments, 2 / (1 + std::cos(std::numbers::pi / double(segments)))};
    }
};

void require_transform(const Operation& op, const Affine& m) {
    op.require(std::isfinite(m.a) && std::isfinite(m.b) && std::isfinite(m.c) &&
                   std::isfinite(m.d) && std::isfinite(m.e) && std::isfinite(m.f),
               "transform", "transform must be finite");
}

void require_point(const Operation& op, Point point, std::string_view parameter) {
    op.require(std::isfinite(point.x) && std::isfinite(point.y), parameter,
               "coordinates must be finite");
}

void require_extent(const Operation& op, float width, float height) {
    op.require(std::isfinite(width) && width >= 0, "width", "width must be finite and nonnegative");
    op.require(std::isfinite(height) && height >= 0, "height",
               "height must be finite and nonnegative");
}

struct ShapeStyle {
    SelectionMode mode;
    FillRule rule;
    bool anti_alias;
    float feather;
};

void require_style(const Operation& op, const ShapeStyle& style, const Affine& transform) {
    require_mode(op, style.mode);
    op.require(std::to_underlying(style.rule) <= std::to_underlying(FillRule::even_odd), "rule",
               "unknown fill rule");
    require_transform(op, transform);
    op.require(std::isfinite(style.feather) && style.feather >= 0 && style.feather <= 1000,
               "feather", "feather must be finite and in [0, 1000]");
}

// Edge pieces for the kernel: clipped to the rows that can see them, cut at every row
// boundary (collinear, sharing exact endpoints) and sorted by top y, so a workgroup
// finds the pieces of its row by binary search. Edges wholly right of
// the mask never affect it and are dropped.
constexpr std::size_t piece_limit = std::size_t{1} << 22;
constexpr double band_capacity = 256; // select_shape.wgsl
// Upper bound on the estimated kernel work for one shape, about a second on an
// integrated GPU. Only pathological shapes (hundreds of pieces crossing many rows at
// once) reach it; they are rejected instead of stalling the device.
constexpr double work_limit = 2.5e9;

std::vector<float> edge_pieces(const Operation& op, const Path& path, const Resource& mask) {
    const double height = mask.height, width = mask.width;
    std::vector<std::array<float, 4>> pieces;
    // Edges wholly left of the mask only add winding: they are replaced by the fewest
    // vertical edges at x = -1 with the same net winding, so contours far off the left
    // side cost nothing per pixel.
    std::vector<std::array<double, 4>> edges;
    std::vector<std::pair<double, int>> changes;
    for (const auto& edge : path.edges) {
        const auto [x0, y0, x1, y1] = edge;
        if (std::max(x0, x1) < 0 && y0 != y1 && std::isfinite(x0) && std::isfinite(x1)) {
            const double top = std::clamp(std::min(y0, y1), -1.0, height + 1);
            const double bottom = std::clamp(std::max(y0, y1), -1.0, height + 1);
            const int direction = y1 > y0 ? 1 : -1;
            changes.push_back({top, direction});
            changes.push_back({bottom, -direction});
        } else {
            edges.push_back(edge);
        }
    }
    std::ranges::sort(changes);
    int winding = 0;
    for (std::size_t i = 0; i + 1 < changes.size(); ++i) {
        winding += changes[i].second;
        const double top = changes[i].first, bottom = changes[i + 1].first;
        for (int k = 0; top < bottom && k < std::abs(winding); ++k) {
            edges.push_back(winding > 0 ? std::array{-1.0, top, -1.0, bottom}
                                        : std::array{-1.0, bottom, -1.0, top});
        }
    }
    for (auto [x0, y0, x1, y1] : edges) {
        op.require(std::isfinite(x0) && std::isfinite(y0) && std::isfinite(x1) &&
                       std::isfinite(y1) && std::abs(x0) < 1e9 && std::abs(x1) < 1e9 &&
                       std::abs(y0) < 1e9 && std::abs(y1) < 1e9,
                   "transform", "transformed coordinates must be finite and below 1e9");
        const double top = std::max(std::min(y0, y1), -1.0);
        const double bottom = std::min(std::max(y0, y1), height + 1);
        if (top > bottom || (y0 == y1 && (y0 <= 0 || y0 >= height)) || std::min(x0, x1) >= width) {
            continue;
        }
        const auto at = [&](double y) {
            return y0 == y1 ? x0 : x0 + (y - y0) * (x1 - x0) / (y1 - y0);
        };
        // Keep the original direction while clipping.
        const double from_y = y0 <= y1 ? top : bottom, to_y = y0 <= y1 ? bottom : top;
        const double from_x = y0 == y1 ? x0 : at(from_y), to_x = y0 == y1 ? x1 : at(to_y);
        // Cut at every integer row boundary, so pieces never cross rows and the ends a
        // row sees are only real vertices.
        const auto first_row = std::floor(top) + 1, last_row = std::ceil(bottom) - 1;
        const auto count = std::size_t(std::max(0.0, last_row - first_row + 1)) + 1;
        op.require(pieces.size() + count <= piece_limit, "points",
                   "shape has too many edge pieces for this mask", ErrorCode::capacity);
        std::array<float, 2> previous{float(from_x), float(from_y)};
        for (std::size_t i = 1; i <= count; ++i) {
            std::array<float, 2> next{float(to_x), float(to_y)};
            if (i < count && y0 != y1) {
                const double y = y0 <= y1 ? first_row + double(i - 1) : last_row - double(i - 1);
                next = {float(at(y)), float(y)};
            }
            if (previous != next) {
                pieces.push_back({previous[0], previous[1], next[0], next[1]});
            }
            previous = next;
        }
    }
    // Work per row (select_shape.wgsl): every run scans the row's pieces; the crossings
    // are sorted; rows with more pieces than workgroup memory scan them per pixel; edge
    // pixels use short lists, or scan the row when it has too many vertices.
    std::vector<double> starting(std::size_t(height) + 2), spread(std::size_t(height) + 2),
        vertices(std::size_t(height) + 2);
    for (const auto& piece : pieces) {
        const double low = std::min(piece[1], piece[3]), high = std::max(piece[1], piece[3]);
        const auto row = std::size_t(std::clamp(std::floor(low) + 1.0, 0.0, height + 1));
        starting[row] += 1;
        spread[row] += std::abs(piece[2] - piece[0]) + 2;
        vertices[row] += low != high && (low != std::floor(low) || high != std::floor(low) + 1);
    }
    const double runs = std::ceil(width / 256);
    double work = 0;
    for (std::size_t row = 1; row <= std::size_t(height); ++row) {
        const double n = starting[row], reach = spread[row];
        const double corners = vertices[row];
        work += n * (2 * runs + 8) + n * n / 64 + (n > band_capacity ? width * n : 0) +
                reach * (16 + (corners > 64 ? n : 2 * corners));
    }
    op.require(work <= work_limit, "points", "shape is too complex for this mask size",
               ErrorCode::capacity);
    std::ranges::sort(pieces, {}, [](const auto& e) { return std::min(e[1], e[3]); });
    std::vector<float> words;
    words.reserve(pieces.size() * 4);
    for (const auto& piece : pieces) {
        words.insert(words.end(), piece.begin(), piece.end());
    }
    return words;
}

void record_shape(Operation& op, const std::shared_ptr<Resource>& mask,
                  const std::shared_ptr<WorkspaceStorage>& workspace, const Path& path,
                  const ShapeStyle& style) {
    const auto edges = edge_pieces(op, path, *mask);
    const bool feathered = style.feather > 0;
    // Feathered shapes rasterize into the coverage plane of feather's scratch.
    const auto scratch = scratch_view(
        op, workspace, feathered ? feather_scratch(size_of(*mask), op.name()) : ImageSize{});
    const auto& target = feathered ? scratch : mask;
    auto shape = kernel_record(Kernel::select_shape, target);
    // One 256-invocation workgroup per row.
    shape.parameters.offsets = {static_cast<std::int32_t>(mask->width),
                                static_cast<std::int32_t>(mask->height), 0, 0};
    shape.parameters.reserved = {feathered ? 0u : std::to_underlying(style.mode),
                                 std::to_underlying(style.rule), style.anti_alias ? 1u : 0u, 0};
    shape.parameters.dispatch = linear_dispatch(std::uint64_t(mask->height) * 64);
    op.data(shape, std::span<const float>(edges), "points");
    if (!feathered) {
        op.append({shape});
        return;
    }
    const auto passes = feather_records(op, target, mask, style.feather, false, style.mode);
    op.reserve(1 + passes.size());
    op.append({shape});
    for (const auto& pass : passes) {
        op.append({pass});
    }
}
} // namespace

void Commands::select_rectangle(const Mask& destination, const RectangleSelectionOptions& options) {
    Operation op(recording_.get(), "select_rectangle");
    const auto& mask = op.resource(destination.resource_, "destination", ResourceKind::mask);
    const ShapeStyle style{options.mode, FillRule::nonzero, options.anti_alias, options.feather};
    require_style(op, style, options.transform);
    require_point(op, options.origin, "origin");
    require_extent(op, options.width, options.height);
    op.require(std::isfinite(options.corner_radius) && options.corner_radius >= 0, "corner_radius",
               "corner radius must be finite and nonnegative");
    Path path{options.transform, {}};
    const double x = options.origin.x, y = options.origin.y;
    const double width = options.width, height = options.height;
    const double radius = std::min({double(options.corner_radius), width / 2, height / 2});
    if (width > 0 && height > 0 && radius <= 0) {
        const std::array<std::array<double, 2>, 4> corners{
            {{x, y}, {x + width, y}, {x + width, y + height}, {x, y + height}}};
        path.polygon(corners);
    } else if (width > 0 && height > 0) {
        const auto [segments, outward] = path.turn(radius);
        const auto quarter = std::max<std::size_t>((segments + 3) / 4, 2);
        const std::array<std::array<double, 2>, 4> centers{
            {{x + width - radius, y + radius},
             {x + width - radius, y + height - radius},
             {x + radius, y + height - radius},
             {x + radius, y + radius}}};
        std::vector<std::array<double, 2>> points;
        for (std::size_t corner = 0; corner < 4; ++corner) {
            // Clockwise on screen, starting at the top of the right edge.
            const double start = (double(corner) - 1) * std::numbers::pi / 2;
            for (std::size_t i = 0; i <= quarter; ++i) {
                const double angle = start + double(i) / double(quarter) * std::numbers::pi / 2;
                const double reach = i == 0 || i == quarter ? radius : radius * outward;
                points.push_back({centers[corner][0] + reach * std::cos(angle),
                                  centers[corner][1] + reach * std::sin(angle)});
            }
        }
        path.polygon(points);
    }
    record_shape(op, mask, options.workspace.storage_, path, style);
}

void Commands::select_ellipse(const Mask& destination, const EllipseSelectionOptions& options) {
    Operation op(recording_.get(), "select_ellipse");
    const auto& mask = op.resource(destination.resource_, "destination", ResourceKind::mask);
    const ShapeStyle style{options.mode, FillRule::nonzero, options.anti_alias, options.feather};
    require_style(op, style, options.transform);
    require_point(op, options.origin, "origin");
    require_extent(op, options.width, options.height);
    Path path{options.transform, {}};
    if (options.width > 0 && options.height > 0) {
        const double rx = options.width / 2.0, ry = options.height / 2.0;
        const double cx = options.origin.x + rx, cy = options.origin.y + ry;
        const auto [segments, outward] = path.turn(std::max(rx, ry));
        std::vector<std::array<double, 2>> points(segments);
        for (std::size_t i = 0; i < segments; ++i) {
            const double angle = 2 * std::numbers::pi * double(i) / double(segments);
            points[i] = {cx + outward * rx * std::cos(angle), cy + outward * ry * std::sin(angle)};
        }
        path.polygon(points);
    }
    record_shape(op, mask, options.workspace.storage_, path, style);
}

void Commands::select_polygon(const Mask& destination, const PolygonSelectionOptions& options) {
    Operation op(recording_.get(), "select_polygon");
    const auto& mask = op.resource(destination.resource_, "destination", ResourceKind::mask);
    const ShapeStyle style{options.mode, options.rule, options.anti_alias, options.feather};
    require_style(op, style, options.transform);
    for (const auto point : options.points) {
        require_point(op, point, "points");
    }
    std::uint64_t total = 0;
    for (const auto count : options.contours) {
        total += count;
    }
    op.require(options.contours.empty() || total == options.points.size(), "contours",
               "contour point counts must sum to the number of points");
    op.require(options.points.size() <= std::numeric_limits<std::uint32_t>::max() / 4, "points",
               "polygon has too many points", ErrorCode::capacity);
    Path path{options.transform, {}};
    path.edges.reserve(options.points.size());
    const auto contour = [&](std::span<const Point> points) {
        std::vector<std::array<double, 2>> mapped;
        mapped.reserve(points.size());
        for (const auto point : points) {
            mapped.push_back(path.map(point.x, point.y));
        }
        path.contour(mapped);
    };
    if (options.contours.empty()) {
        contour(options.points);
    }
    std::size_t first = 0;
    for (const auto count : options.contours) {
        contour(options.points.subspan(first, count));
        first += count;
    }
    record_shape(op, mask, options.workspace.storage_, path, style);
}
} // namespace wgpupixel
