#include "brush_engine.h"
#include "../utils/workspace.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace wgpupixel {
using namespace detail;
namespace {
constexpr std::size_t dab_limit = std::size_t{1} << 22;
constexpr double degrees = 180.0 / 3.14159265358979323846;

void check(bool condition, std::string_view operation, std::string_view parameter,
           std::string_view message, ErrorCode code = ErrorCode::invalid_argument) {
    if (!condition) {
        fail(code, operation, parameter, message);
    }
}

bool within(float value, float low, float high) {
    return value >= low && value <= high; // False for NaN.
}

void validate(const Brush& brush, std::string_view operation) {
    check(std::isfinite(brush.diameter) && brush.diameter >= 1, operation, "brush.diameter",
          "diameter must be finite and at least 1");
    check(std::isfinite(brush.angle), operation, "brush.angle", "angle must be finite");
    check(within(brush.roundness, 0, 1) && brush.roundness > 0, operation, "brush.roundness",
          "roundness must lie in (0, 1]");
    check(within(brush.minimum_roundness, 0, 1) && brush.minimum_roundness > 0, operation,
          "brush.minimum_roundness", "minimum roundness must lie in (0, 1]");
    check(within(brush.spacing, 0, 10), operation, "brush.spacing", "spacing must lie in [0, 10]");
    check(within(brush.scatter, 0, 10), operation, "brush.scatter", "scatter must lie in [0, 10]");
    check(std::to_underlying(brush.angle_control) <=
              std::to_underlying(BrushAngleControl::direction),
          operation, "brush.angle_control", "angle control is invalid");
    const std::pair<float, std::string_view> fractions[] = {
        {brush.hardness, "brush.hardness"},
        {brush.minimum_size, "brush.minimum_size"},
        {brush.minimum_opacity, "brush.minimum_opacity"},
        {brush.minimum_flow, "brush.minimum_flow"},
        {brush.size_jitter, "brush.size_jitter"},
        {brush.opacity_jitter, "brush.opacity_jitter"},
        {brush.flow_jitter, "brush.flow_jitter"},
        {brush.angle_jitter, "brush.angle_jitter"},
        {brush.roundness_jitter, "brush.roundness_jitter"}};
    for (const auto& [value, parameter] : fractions) {
        check(within(value, 0, 1), operation, parameter, "value must lie in [0, 1]");
    }
}

std::uint32_t mix32(std::uint32_t value) {
    value = (value ^ (value >> 16)) * 0x7feb352du;
    value = (value ^ (value >> 15)) * 0x846ca68bu;
    return value ^ (value >> 16);
}

// Uniform in [0, 1) per seed, dab and channel.
double random(std::uint32_t seed, std::size_t dab, std::uint32_t channel) {
    const auto key = mix32(static_cast<std::uint32_t>(dab) * 8u + channel + 0x9e3779b9u);
    return (mix32(seed ^ key) >> 8) * (1.0 / 16777216.0);
}

double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

double tip_extent(std::optional<ImageSize> tip) {
    if (!tip) {
        return 1;
    }
    // Half a texel of bilinear support beyond the tip rectangle.
    const double w = double(tip->width), h = double(tip->height);
    return std::hypot(w + 1, h + 1) / std::max(w, h);
}

std::optional<ImageSize> tip_size(const Brush& brush) {
    if (!brush.tip) {
        return std::nullopt;
    }
    return brush.tip->size();
}

struct Bounds {
    double left, top, right, bottom;
};

Bounds dab_bounds(const BrushDab& dab, std::optional<ImageSize> tip) {
    const auto reach = dab_reach(dab, tip);
    return {std::floor(dab.center.x - reach), std::floor(dab.center.y - reach),
            std::ceil(dab.center.x + reach), std::ceil(dab.center.y + reach)};
}
} // namespace

std::vector<BrushDab> detail::place_dabs(std::span<const StrokeSample> samples, const Brush& brush,
                                         std::string_view operation) {
    validate(brush, operation);
    for (const auto& sample : samples) {
        check(std::isfinite(sample.position.x) && std::isfinite(sample.position.y) &&
                  std::isfinite(sample.rotation) && within(sample.pressure, 0, 1) &&
                  within(sample.tilt, 0, 1),
              operation, "samples",
              "samples need finite positions and rotation, pressure and tilt in [0, 1]");
    }
    std::vector<BrushDab> dabs;
    if (samples.empty()) {
        return dabs;
    }
    const auto emit = [&](double x, double y, double pressure, double tilt, double rotation,
                          double direction) {
        const auto index = dabs.size();
        check(index < dab_limit, operation, "samples", "stroke places too many dabs",
              ErrorCode::capacity);
        const auto r = [&](std::uint32_t channel) { return random(brush.seed, index, channel); };
        double size =
            brush.diameter * lerp(brush.minimum_size, 1, pressure) * (1 - brush.size_jitter * r(0));
        const double opacity =
            lerp(brush.minimum_opacity, 1, pressure) * (1 - brush.opacity_jitter * r(1));
        double flow = lerp(brush.minimum_flow, 1, pressure) * (1 - brush.flow_jitter * r(2));
        double angle = brush.angle + brush.angle_jitter * 180 * (2 * r(3) - 1);
        if (brush.angle_control == BrushAngleControl::rotation) {
            angle += rotation;
        } else if (brush.angle_control == BrushAngleControl::direction) {
            angle += direction;
        }
        const double toward = std::clamp(
            brush.roundness_jitter * r(4) + (brush.tilt_roundness ? tilt : 0.0), 0.0, 1.0);
        const double roundness = brush.roundness * lerp(1, brush.minimum_roundness, toward);
        if (brush.scatter > 0) {
            const auto [sine, cosine] = sin_cos_degrees(direction);
            const double across = brush.scatter * size * (2 * r(5) - 1);
            x -= sine * across;
            y += cosine * across;
            if (brush.scatter_both_axes) {
                const double along = brush.scatter * size * (2 * r(6) - 1);
                x += cosine * along;
                y += sine * along;
            }
        }
        if (size < 1) {
            // Keep one pixel and conserve the painted area.
            flow *= size * size;
            size = 1;
        }
        dabs.push_back({{float(x), float(y)},
                        float(size),
                        float(roundness),
                        float(std::remainder(angle, 360.0)),
                        float(opacity),
                        float(flow)});
    };
    const auto moves = [&](std::size_t i) {
        return samples[i].position != samples[i - 1].position;
    };
    const auto heading = [&](std::size_t i, double fallback) {
        const double dx = double(samples[i].position.x) - samples[i - 1].position.x;
        const double dy = double(samples[i].position.y) - samples[i - 1].position.y;
        return moves(i) ? std::atan2(dy, dx) * degrees : fallback;
    };
    // The first dab faces the first movement.
    double direction = 0;
    for (std::size_t i = 1; i < samples.size(); ++i) {
        if (moves(i)) {
            direction = heading(i, 0);
            break;
        }
    }
    const auto& first = samples.front();
    emit(first.position.x, first.position.y, first.pressure, first.tilt, first.rotation, direction);
    if (brush.spacing == 0) {
        for (std::size_t i = 1; i < samples.size(); ++i) {
            direction = heading(i, direction);
            const auto& s = samples[i];
            emit(s.position.x, s.position.y, s.pressure, s.tilt, s.rotation, direction);
        }
        return dabs;
    }
    const auto step = [&](double pressure) {
        const double size = brush.diameter * lerp(brush.minimum_size, 1, pressure);
        return std::max(brush.spacing * std::max(size, 1.0), 0.1);
    };
    double remaining = step(first.pressure);
    for (std::size_t i = 1; i < samples.size(); ++i) {
        const auto& a = samples[i - 1];
        const auto& b = samples[i];
        const double dx = double(b.position.x) - a.position.x;
        const double dy = double(b.position.y) - a.position.y;
        const double length = std::hypot(dx, dy);
        if (length == 0) {
            continue;
        }
        direction = std::atan2(dy, dx) * degrees;
        const double turn = std::remainder(double(b.rotation) - a.rotation, 360.0);
        double travelled = 0;
        while (remaining <= length - travelled) {
            travelled += remaining;
            const double t = travelled / length;
            const double pressure = lerp(a.pressure, b.pressure, t);
            emit(a.position.x + dx * t, a.position.y + dy * t, pressure, lerp(a.tilt, b.tilt, t),
                 a.rotation + turn * t, direction);
            remaining = step(pressure);
        }
        remaining -= length - travelled;
    }
    return dabs;
}

double detail::dab_reach(const BrushDab& dab, std::optional<ImageSize> tip) {
    return 0.5 * double(dab.diameter) * tip_extent(tip) + 1;
}

std::int64_t detail::smudge_side(double diameter, std::optional<ImageSize> tip,
                                 std::string_view operation) {
    // Covers floor(center) +- (ceil(reach) + 1) for any fractional center.
    const double side = 2 * (std::ceil(0.5 * diameter * tip_extent(tip) + 1) + 1) + 1;
    // Shaders index the patch area with 32 bits.
    check(side <= 65535, operation, "brush.diameter", "diameter is too large for a smudge patch",
          ErrorCode::capacity);
    return static_cast<std::int64_t>(side);
}

DabShape detail::dab_shape(const BrushDab& dab) {
    const double rx = 0.5 * double(dab.diameter), ry = rx * dab.roundness;
    // Widen sub-pixel axes to the pixel filter and keep the painted area.
    const double wx = std::max(rx, 0.5), wy = std::max(ry, 0.5);
    const auto [sine, cosine] = sin_cos_degrees(dab.angle);
    return {{float(cosine / wx), float(sine / wx), float(-sine / wy), float(cosine / wy)},
            float(rx * ry / (wx * wy))};
}

void detail::require_unit(const Operation& op, float value, std::string_view parameter) {
    op.require(within(value, 0, 1), parameter, "value must lie in [0, 1]");
}

void detail::require_mode(const Operation& op, BlendMode mode) {
    op.require(std::to_underlying(mode) <= std::to_underlying(BlendMode::hard_light), "mode",
               "unknown blend mode");
}

std::optional<Record> detail::stroke_record(Operation& op, const Stroke& stroke, Record record,
                                            const std::optional<Rect>& region) {
    const auto dabs = place_dabs(stroke.samples, stroke.brush, stroke.operation);
    std::optional<ImageSize> tip;
    if (stroke.tip) {
        op.require(stroke.tip->width > 0 && stroke.tip->height > 0, "brush.tip",
                   "tip must not be empty");
        tip = ImageSize{stroke.tip->width, stroke.tip->height};
    }
    auto& p = record.parameters;
    Bounds limit{0, 0, double(p.dimensions[2]), double(p.dimensions[3])};
    if (region) {
        limit.left = std::max(limit.left, double(region->x));
        limit.top = std::max(limit.top, double(region->y));
        limit.right = std::min(limit.right, double(region->x) + region->width);
        limit.bottom = std::min(limit.bottom, double(region->y) + region->height);
    }
    struct Placed {
        std::size_t dab;
        std::array<std::int64_t, 4> area;
    };
    std::vector<Placed> placed;
    std::array<std::int64_t, 4> box{std::numeric_limits<std::int64_t>::max(),
                                    std::numeric_limits<std::int64_t>::max(), 0, 0};
    double reach = 1;
    for (std::size_t i = 0; i < dabs.size(); ++i) {
        const auto& dab = dabs[i];
        if (!(stroke.opacity * dab.opacity > 0 && stroke.flow * dab.flow > 0) ||
            !std::isfinite(dab.center.x) || !std::isfinite(dab.center.y)) {
            continue;
        }
        const auto b = dab_bounds(dab, tip);
        const Bounds clipped{std::max(b.left, limit.left), std::max(b.top, limit.top),
                             std::min(b.right, limit.right), std::min(b.bottom, limit.bottom)};
        if (clipped.left >= clipped.right || clipped.top >= clipped.bottom) {
            continue;
        }
        const std::array area{std::int64_t(clipped.left), std::int64_t(clipped.top),
                              std::int64_t(clipped.right), std::int64_t(clipped.bottom)};
        placed.push_back({i, area});
        box = {std::min(box[0], area[0]), std::min(box[1], area[1]), std::max(box[2], area[2]),
               std::max(box[3], area[3])};
        reach = std::max(reach, dab_reach(dab, tip));
    }
    if (placed.empty()) {
        return std::nullopt;
    }
    // Bin dabs into square tiles about a dab radius wide, so each pixel visits only the
    // dabs near it, in stroke order.
    const auto tile = std::clamp<std::int64_t>(
        std::bit_ceil(static_cast<std::uint64_t>(std::ceil(std::min(reach, 256.0)))), 16, 256);
    const auto columns = (box[2] - box[0] + tile - 1) / tile;
    const auto rows = (box[3] - box[1] + tile - 1) / tile;
    const auto tiles = static_cast<std::size_t>(columns * rows);
    std::vector<std::uint64_t> starts(tiles + 1, 0);
    for (const auto& item : placed) {
        for (auto y = (item.area[1] - box[1]) / tile; y <= (item.area[3] - 1 - box[1]) / tile;
             ++y) {
            for (auto x = (item.area[0] - box[0]) / tile; x <= (item.area[2] - 1 - box[0]) / tile;
                 ++x) {
                ++starts[std::size_t(y * columns + x) + 1];
            }
        }
    }
    for (std::size_t i = 0; i < tiles; ++i) {
        starts[i + 1] += starts[i];
    }
    // Bound the GPU work: each listed dab is evaluated for every pixel of its tile.
    op.require(double(starts[tiles]) * double(tile * tile) <= stroke_evaluation_limit, "samples",
               "stroke needs too many dab evaluations; split it or raise spacing",
               ErrorCode::capacity);
    const std::uint64_t table = 8 * std::uint64_t(placed.size());
    const std::uint64_t list = table + tiles + 1;
    const std::uint64_t total = list + starts[tiles];
    op.require(total <= std::numeric_limits<std::uint32_t>::max(), "samples",
               "stroke data exceeds 32-bit addressing", ErrorCode::capacity);
    const auto words = op.data(record, static_cast<std::size_t>(total), "samples");
    for (std::size_t j = 0; j < placed.size(); ++j) {
        const auto& dab = dabs[placed[j].dab];
        const auto [shape, gain] = dab_shape(dab);
        const std::array<float, 8> values{dab.center.x,
                                          dab.center.y,
                                          shape[0],
                                          shape[1],
                                          shape[2],
                                          shape[3],
                                          stroke.opacity * dab.opacity,
                                          stroke.flow * dab.flow * gain};
        std::ranges::transform(values, words.begin() + std::ptrdiff_t(8 * j),
                               [](float value) { return std::bit_cast<std::uint32_t>(value); });
    }
    for (std::size_t i = 0; i <= tiles; ++i) {
        words[table + i] = static_cast<std::uint32_t>(list + starts[i]);
    }
    auto cursor = starts;
    for (std::size_t j = 0; j < placed.size(); ++j) {
        const auto& area = placed[j].area;
        for (auto y = (area[1] - box[1]) / tile; y <= (area[3] - 1 - box[1]) / tile; ++y) {
            for (auto x = (area[0] - box[0]) / tile; x <= (area[2] - 1 - box[0]) / tile; ++x) {
                words[list + cursor[std::size_t(y * columns + x)]++] =
                    static_cast<std::uint32_t>(j);
            }
        }
    }
    p.dispatch = {static_cast<std::uint32_t>(box[0]), static_cast<std::uint32_t>(box[1]),
                  static_cast<std::uint32_t>(box[2] - box[0]),
                  static_cast<std::uint32_t>(box[3] - box[1])};
    p.offsets = {static_cast<std::int32_t>(box[0]), static_cast<std::int32_t>(box[1]),
                 static_cast<std::int32_t>(tile), static_cast<std::int32_t>(columns)};
    p.reserved[2] = static_cast<std::uint32_t>(table);
    std::uint64_t maximum = 1;
    for (std::size_t i = 0; i < tiles; ++i) {
        maximum = std::max(maximum, starts[i + 1] - starts[i]);
    }
    p.color2 = {stroke.brush.hardness, tip ? float(std::max(tip->width, tip->height)) / 2 : 0,
                float(maximum), 0};
    return record;
}

std::vector<Record> detail::bounded_stroke_records(const Record& record) {
    // Bound each dispatch to 2^22 dab-pixel evaluations, including dense tiles.
    const auto budget = std::max(1u, std::uint32_t((1u << 22) / record.parameters.color2[2]));
    const auto& box = record.parameters.dispatch;
    const auto width = std::min(box[2], budget);
    const auto height = std::max(1u, budget / width);
    std::vector<Record> records;
    for (std::uint32_t y = 0; y < box[3]; y += height) {
        for (std::uint32_t x = 0; x < box[2]; x += width) {
            auto part = record;
            part.parameters.dispatch = {box[0] + x, box[1] + y, std::min(width, box[2] - x),
                                        std::min(height, box[3] - y)};
            part.starts_batch = records.size() % 32 == 0;
            records.push_back(std::move(part));
        }
    }
    return records;
}

void detail::append_stroke(Operation& op, const Record& record) {
    const auto parts = bounded_stroke_records(record);
    op.reserve(parts.size());
    for (const auto& part : parts) {
        op.append({part});
    }
}

std::size_t brush_dabs(std::span<const StrokeSample> samples, const Brush& brush,
                       std::span<BrushDab> dabs) {
    const auto placed = place_dabs(samples, brush, "brush_dabs");
    std::copy_n(placed.begin(), std::min(placed.size(), dabs.size()), dabs.begin());
    return placed.size();
}

std::optional<Rect> stroke_bounds(std::span<const StrokeSample> samples, const Brush& brush) {
    const auto dabs = place_dabs(samples, brush, "stroke_bounds");
    const auto tip = tip_size(brush);
    std::optional<Bounds> box;
    for (const auto& dab : dabs) {
        const auto b = dab_bounds(dab, tip);
        box = box ? Bounds{std::min(box->left, b.left), std::min(box->top, b.top),
                           std::max(box->right, b.right), std::max(box->bottom, b.bottom)}
                  : b;
    }
    if (!box) {
        return std::nullopt;
    }
    constexpr double low = std::numeric_limits<std::int32_t>::min();
    constexpr double high = std::numeric_limits<std::int32_t>::max();
    const double left = std::clamp(box->left, low, high);
    const double top = std::clamp(box->top, low, high);
    return Rect{static_cast<std::int32_t>(left), static_cast<std::int32_t>(top),
                static_cast<std::int32_t>(std::clamp(box->right - left, 0.0, high)),
                static_cast<std::int32_t>(std::clamp(box->bottom - top, 0.0, high))};
}

WorkspacePlan detail::stroke_workspace(ImageSize size, std::string_view operation) {
    check(size.width > 0 && size.height > 0 && size.width <= INT32_MAX && size.height <= INT32_MAX,
          operation, "destination", "dimensions must be positive and fit int32");
    const auto pixels = std::uint64_t(size.width) * std::uint64_t(size.height);
    check(pixels <= UINT32_MAX, operation, "destination", "pixel count exceeds uint32",
          ErrorCode::capacity);
    return workspace_plan({pixels * 16}, operation);
}

OperationRequirements focus_stroke_requirements(ImageSize destination,
                                                const FocusStrokeOptions& options) {
    constexpr auto name = "focus_stroke_requirements";
    validate(options.brush, name);
    return {destination, stroke_workspace(destination, name)};
}

OperationRequirements smudge_stroke_requirements(ImageSize destination,
                                                 const SmudgeStrokeOptions& options) {
    constexpr auto name = "smudge_stroke_requirements";
    (void)stroke_workspace(destination, name);
    validate(options.brush, name);
    const auto side = smudge_side(options.brush.diameter, tip_size(options.brush), name);
    return {destination, stroke_workspace({side, side}, name)};
}
} // namespace wgpupixel
