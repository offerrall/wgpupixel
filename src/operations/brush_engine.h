#pragma once
#include "../utils/operations.h"
#include <optional>
#include <vector>

namespace wgpupixel::detail {
// Places dabs as documented at Brush; failures name `operation`.
std::vector<BrushDab> place_dabs(std::span<const StrokeSample> samples, const Brush& brush,
                                 std::string_view operation);
// One RGBA workspace plane; validates dimensions and shader indexing capacity.
WorkspacePlan stroke_workspace(ImageSize size, std::string_view operation);
// Largest distance from a dab center to a covered pixel center, including the
// antialiased rim.
double dab_reach(const BrushDab& dab, std::optional<ImageSize> tip);

// Side of the square smudge patch holding dabs up to this diameter; fails for
// `operation` when it does not fit a 32-bit image dimension.
std::int64_t smudge_side(double diameter, std::optional<ImageSize> tip, std::string_view operation);

// Inverse dab shape, mapping an offset from the center into unit tip coordinates, and a
// coverage gain. Axes below one pixel keep one pixel and the gain conserves the area,
// so thin tips paint their true area instead of a full-coverage line.
struct DabShape {
    std::array<float, 4> matrix;
    float gain;
};
DabShape dab_shape(const BrushDab& dab);

// Pathological-input limits, documented at Brush and SmudgeStrokeOptions. Measured on
// an integrated GPU: 2^34 dab evaluations take about 2 s; smudge is memory bound at about
// 2 ns per patch update, so 2^32 updates take about 8 s. A 5000 px smudge at 10% spacing
// across a 6000 px canvas needs 3e8 updates.
inline constexpr double stroke_evaluation_limit = double(std::uint64_t{1} << 34);
inline constexpr double smudge_evaluation_limit = double(std::uint64_t{1} << 32);
// Smudge records: patches above this area give each dab its own multi-workgroup
// dispatch; smaller ones share one workgroup for up to smudge_chunk_updates slots.
inline constexpr std::uint64_t smudge_wide_area = 128 * 128;
inline constexpr std::uint64_t smudge_chunk_updates = std::uint64_t{1} << 22;

struct Stroke {
    std::span<const StrokeSample> samples;
    const Brush& brush;
    const Resource* tip; // Validated sampled tip, or null.
    float opacity, flow;
    std::string_view operation;
};

// Completes a brush kernel record: stroke bounds as dispatch (inside region), the dab
// list binned into tiles staged as data, and engine parameters. Empty when the stroke
// cannot change a pixel. Tools own params.reserved x/y/w, values, color1 and extra*.
std::optional<Record> stroke_record(Operation& op, const Stroke& stroke, Record record,
                                    const std::optional<Rect>& region);

// Split disjoint output rectangles; each keeps the complete ordered dab list.
std::vector<Record> bounded_stroke_records(const Record& record);
void append_stroke(Operation& op, const Record& record);

void require_unit(const Operation& op, float value, std::string_view parameter);
void require_mode(const Operation& op, BlendMode mode);
} // namespace wgpupixel::detail
