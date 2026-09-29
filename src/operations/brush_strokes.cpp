#include "brush_engine.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace wgpupixel {
using namespace detail;
namespace {
// Tool selectors, params.reserved.x of the paint and sample kernels.
enum PaintTool : std::uint32_t { paint_color, paint_erase, paint_dodge_burn, paint_sponge };
enum SampleTool : std::uint32_t { sample_clone, sample_pattern, sample_blur, sample_sharpen };

std::shared_ptr<Resource> tip_resource(const Operation& op, const std::shared_ptr<Resource>* tip) {
    return tip ? op.resource(*tip, "brush.tip", ResourceKind::mask) : nullptr;
}

// In-place tools. The tip, when sampled, binds as the kernel source.
Record paint_record(const std::shared_ptr<Resource>& tip,
                    const std::shared_ptr<Resource>& destination) {
    return tip ? kernel_record(Kernel::brush_paint_tip, tip, destination)
               : kernel_record(Kernel::brush_paint, destination);
}

// Tools reading another image, bound as source or, with a sampled tip, at binding 4.
Record sample_record(const std::shared_ptr<Resource>& tip, const std::shared_ptr<Resource>& image,
                     const std::shared_ptr<Resource>& destination) {
    if (!tip) {
        return kernel_record(Kernel::brush_sample, image, destination);
    }
    auto record = kernel_record(Kernel::brush_sample_tip, tip, destination);
    record.sources[0] = image;
    record.parameters.extra2[2] = std::bit_cast<float>(image->width);
    record.parameters.extra2[3] = std::bit_cast<float>(image->height);
    return record;
}
} // namespace

void Commands::brush_stroke(const Image& destination, const BrushStrokeOptions& options) {
    Operation op(recording_.get(), "brush_stroke");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& pixels = op.resource(destination.resource_, "destination");
    const auto tip = tip_resource(op, options.brush.tip ? &options.brush.tip->resource_ : nullptr);
    require_color(op, options.color, "color");
    require_mode(op, options.mode);
    require_unit(op, options.opacity, "opacity");
    require_unit(op, options.flow, "flow");
    auto record = stroke_record(
        op,
        {options.samples, options.brush, tip.get(), options.opacity, options.flow, "brush_stroke"},
        paint_record(tip, pixels), options.region);
    if (!record) {
        return;
    }
    record->parameters.reserved[0] = paint_color;
    record->parameters.reserved[1] = std::to_underlying(options.mode);
    record->parameters.color1 = rgba(options.color);
    record->parameters.reserved[3] |= options.preserve_alpha ? 256u : 0u;
    append_stroke(op, *record);
}

void Commands::eraser_stroke(const Image& destination, const EraserStrokeOptions& options) {
    Operation op(recording_.get(), "eraser_stroke");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& pixels = op.resource(destination.resource_, "destination");
    const auto tip = tip_resource(op, options.brush.tip ? &options.brush.tip->resource_ : nullptr);
    require_unit(op, options.opacity, "opacity");
    require_unit(op, options.flow, "flow");
    auto record = stroke_record(
        op,
        {options.samples, options.brush, tip.get(), options.opacity, options.flow, "eraser_stroke"},
        paint_record(tip, pixels), options.region);
    if (!record) {
        return;
    }
    record->parameters.reserved[0] = paint_erase;
    append_stroke(op, *record);
}

void Commands::clone_stroke(const Image& source, const Image& destination,
                            const CloneStrokeOptions& options) {
    Operation op(recording_.get(), "clone_stroke");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& image = op.resource(source.resource_, "source");
    const auto& pixels = op.resource(destination.resource_, "destination");
    op.distinct(*image, *pixels);
    const auto tip = tip_resource(op, options.brush.tip ? &options.brush.tip->resource_ : nullptr);
    op.require(std::isfinite(options.offset.x) && std::isfinite(options.offset.y), "offset",
               "offset must be finite");
    require_mode(op, options.mode);
    require_unit(op, options.opacity, "opacity");
    require_unit(op, options.flow, "flow");
    auto record = stroke_record(
        op,
        {options.samples, options.brush, tip.get(), options.opacity, options.flow, "clone_stroke"},
        sample_record(tip, image, pixels), options.region);
    if (!record) {
        return;
    }
    auto& p = record->parameters;
    p.reserved[0] = sample_clone;
    p.reserved[1] = std::to_underlying(options.mode);
    p.extra[0] = options.offset.x;
    p.extra[1] = options.offset.y;
    record->parameters.reserved[3] |= options.preserve_alpha ? 256u : 0u;
    append_stroke(op, *record);
}

void Commands::pattern_stroke(const Image& pattern, const Image& destination,
                              const PatternStrokeOptions& options) {
    Operation op(recording_.get(), "pattern_stroke");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& image = op.resource(pattern.resource_, "pattern");
    const auto& pixels = op.resource(destination.resource_, "destination");
    op.distinct(*image, *pixels, "pattern");
    const auto tip = tip_resource(op, options.brush.tip ? &options.brush.tip->resource_ : nullptr);
    const auto inverse_transform = inverse(options.transform);
    op.require(inverse_transform.has_value(), "transform", "transform must be invertible");
    require_mode(op, options.mode);
    require_unit(op, options.opacity, "opacity");
    require_unit(op, options.flow, "flow");
    auto record = stroke_record(op,
                                {options.samples, options.brush, tip.get(), options.opacity,
                                 options.flow, "pattern_stroke"},
                                sample_record(tip, image, pixels), options.region);
    if (!record) {
        return;
    }
    auto& p = record->parameters;
    const auto& m = *inverse_transform;
    p.reserved[0] = sample_pattern;
    p.reserved[1] = std::to_underlying(options.mode);
    p.extra = {m.a, m.b, m.c, m.d};
    p.extra2[0] = m.e;
    p.extra2[1] = m.f;
    record->parameters.reserved[3] |= options.preserve_alpha ? 256u : 0u;
    append_stroke(op, *record);
}

void Commands::dodge_burn_stroke(const Image& destination, const DodgeBurnStrokeOptions& options) {
    Operation op(recording_.get(), "dodge_burn_stroke");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& pixels = op.resource(destination.resource_, "destination");
    const auto tip = tip_resource(op, options.brush.tip ? &options.brush.tip->resource_ : nullptr);
    op.require(std::to_underlying(options.range) <= std::to_underlying(ToneRange::highlights),
               "range", "tone range is invalid");
    require_unit(op, options.exposure, "exposure");
    require_unit(op, options.flow, "flow");
    auto record = stroke_record(op,
                                {options.samples, options.brush, tip.get(), options.exposure,
                                 options.flow, "dodge_burn_stroke"},
                                paint_record(tip, pixels), options.region);
    if (!record) {
        return;
    }
    auto& p = record->parameters;
    p.reserved[0] = paint_dodge_burn;
    p.reserved[1] = std::to_underlying(options.range);
    p.reserved[3] = (options.burn ? 1u : 0u) | (options.protect_tones ? 2u : 0u);
    append_stroke(op, *record);
}

void Commands::sponge_stroke(const Image& destination, const SpongeStrokeOptions& options) {
    Operation op(recording_.get(), "sponge_stroke");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& pixels = op.resource(destination.resource_, "destination");
    const auto tip = tip_resource(op, options.brush.tip ? &options.brush.tip->resource_ : nullptr);
    require_unit(op, options.flow, "flow");
    auto record = stroke_record(
        op, {options.samples, options.brush, tip.get(), 1, options.flow, "sponge_stroke"},
        paint_record(tip, pixels), options.region);
    if (!record) {
        return;
    }
    auto& p = record->parameters;
    p.reserved[0] = paint_sponge;
    p.reserved[3] = (options.saturate ? 1u : 0u) | (options.vibrance ? 2u : 0u);
    append_stroke(op, *record);
}

void Commands::focus_stroke(const Image& destination, const FocusStrokeOptions& options) {
    Operation op(recording_.get(), "focus_stroke");
    const auto& pixels = op.resource(destination.resource_, "destination");
    // The pre-stroke copy must ignore the selection, so validate it before recording.
    if (options.mask) {
        op.same_size(*op.resource(options.mask->resource_, "mask", ResourceKind::mask), *pixels,
                     "mask");
    }
    op.require(!options.region || (options.region->width >= 0 && options.region->height >= 0),
               "region", "region dimensions must be nonnegative");
    const auto tip = tip_resource(op, options.brush.tip ? &options.brush.tip->resource_ : nullptr);
    require_unit(op, options.strength, "strength");
    require_unit(op, options.flow, "flow");
    op.workspace(options.workspace.storage_,
                 stroke_workspace({pixels->width, pixels->height}, "focus_stroke"));
    const auto copy = op.temporary(pixels->width, pixels->height);
    auto record = stroke_record(
        op,
        {options.samples, options.brush, tip.get(), options.strength, options.flow, "focus_stroke"},
        sample_record(tip, copy, pixels), options.region);
    if (!record) {
        return;
    }
    auto& p = record->parameters;
    p.reserved[0] = options.sharpen ? sample_sharpen : sample_blur;
    // Copy the stroke area plus the 5x5 filter support.
    auto snapshot = kernel_record(Kernel::copy_image, pixels, copy);
    const auto left = std::max<std::int64_t>(0, std::int64_t(p.dispatch[0]) - 2);
    const auto top = std::max<std::int64_t>(0, std::int64_t(p.dispatch[1]) - 2);
    const auto right =
        std::min<std::int64_t>(pixels->width, std::int64_t(p.dispatch[0]) + p.dispatch[2] + 2);
    const auto bottom =
        std::min<std::int64_t>(pixels->height, std::int64_t(p.dispatch[1]) + p.dispatch[3] + 2);
    snapshot.parameters.dispatch = {
        static_cast<std::uint32_t>(left), static_cast<std::uint32_t>(top),
        static_cast<std::uint32_t>(right - left), static_cast<std::uint32_t>(bottom - top)};
    record->parameters.reserved[3] |= options.preserve_alpha ? 256u : 0u;
    const auto parts = bounded_stroke_records(*record);
    op.reserve(1 + parts.size());
    op.append({snapshot});
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    for (const auto& part : parts) {
        op.append({part});
    }
}

void Commands::smudge_stroke(const Image& destination, const SmudgeStrokeOptions& options) {
    Operation op(recording_.get(), "smudge_stroke");
    // One workgroup runs the dabs in order, so the region clips inside the kernel.
    op.coverage(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                destination.resource_, options.mask != nullptr);
    op.require(!options.region || (options.region->width >= 0 && options.region->height >= 0),
               "region", "region dimensions must be nonnegative");
    const auto& pixels = op.resource(destination.resource_, "destination");
    const auto tip = tip_resource(op, options.brush.tip ? &options.brush.tip->resource_ : nullptr);
    require_unit(op, options.strength, "strength");
    require_color(op, options.color, "color");
    const auto dabs = place_dabs(options.samples, options.brush, "smudge_stroke");
    std::optional<ImageSize> tip_size;
    if (tip) {
        op.require(tip->width > 0 && tip->height > 0, "brush.tip", "tip must not be empty");
        tip_size = ImageSize{tip->width, tip->height};
    }
    const auto required = smudge_side(options.brush.diameter, tip_size, "smudge_stroke");
    op.workspace(options.workspace.storage_,
                 stroke_workspace({required, required}, "smudge_stroke"));
    const auto carried = op.temporary(required, required);
    std::int64_t left = 0, top = 0, right = pixels->width, bottom = pixels->height;
    if (options.region) {
        left = std::max<std::int64_t>(left, options.region->x);
        top = std::max<std::int64_t>(top, options.region->y);
        right =
            std::min<std::int64_t>(right, std::int64_t(options.region->x) + options.region->width);
        bottom = std::min<std::int64_t>(bottom,
                                        std::int64_t(options.region->y) + options.region->height);
    }
    std::vector<BrushDab> kept;
    std::int64_t write_left = right, write_top = bottom, write_right = left, write_bottom = top;
    float largest = 1;
    for (const auto& dab : dabs) {
        if (!std::isfinite(dab.center.x) || !std::isfinite(dab.center.y) ||
            std::abs(dab.center.x) > 1e9f || std::abs(dab.center.y) > 1e9f) {
            continue;
        }
        const auto reach = dab_reach(dab, tip_size);
        // Conservative write bounds, also used by continuation snapshots. Pigment
        // pickup still reads the complete patch, including outside this rectangle.
        const auto l = std::max(double(left), std::floor(dab.center.x - reach));
        const auto t = std::max(double(top), std::floor(dab.center.y - reach));
        const auto r = std::min(double(right), std::ceil(dab.center.x + reach));
        const auto b = std::min(double(bottom), std::ceil(dab.center.y + reach));
        if (l < r && t < b) {
            write_left = std::min(write_left, std::int64_t(l));
            write_top = std::min(write_top, std::int64_t(t));
            write_right = std::max(write_right, std::int64_t(r));
            write_bottom = std::max(write_bottom, std::int64_t(b));
        }
        largest = std::max(largest, dab.diameter);
        kept.push_back(dab);
    }
    if (kept.size() < 2 || left >= right || top >= bottom ||
        write_left >= write_right || write_top >= write_bottom) {
        return;
    }
    const auto side = smudge_side(largest, tip_size, "smudge_stroke");
    const auto area = std::uint64_t(side) * std::uint64_t(side);
    op.require(double(kept.size()) * double(area) <= smudge_evaluation_limit, "samples",
               "smudge needs too many patch updates; split it or raise spacing",
               ErrorCode::capacity);
    // Small patches run chunks of dabs in one workgroup; large ones spread each dab over
    // many workgroups. Both keep every record's work bounded.
    const bool wide = area > smudge_wide_area;
    const auto chunk =
        wide ? std::size_t{1}
             : static_cast<std::size_t>(std::max<std::uint64_t>(1, smudge_chunk_updates / area));
    constexpr std::uint32_t patch_tile = 1024;
    const auto across = (side + patch_tile - 1) / patch_tile;
    const auto count_records =
        wide ? kept.size() * across * across : (kept.size() + chunk - 1) / chunk;
    op.reserve(count_records);
    std::vector<Record> records;
    records.reserve(count_records);
    auto base = tip ? kernel_record(wide ? Kernel::smudge_wide_tip : Kernel::smudge_tip, tip,
                                    pixels)
                    : kernel_record(wide ? Kernel::smudge_wide : Kernel::smudge, carried, pixels);
    if (tip) {
        base.sources[0] = carried;
    }
    auto& p = base.parameters;
    const auto groups = static_cast<std::uint32_t>((side + 1) / 2);
    p.dispatch = wide ? std::array<std::uint32_t, 4>{0, 0, groups, groups}
                      : std::array<std::uint32_t, 4>{0, 0, 1, 1};
    p.offsets = {static_cast<std::int32_t>(write_left), static_cast<std::int32_t>(write_top),
                 static_cast<std::int32_t>(write_right), static_cast<std::int32_t>(write_bottom)};
    p.values[0] = options.strength;
    p.color1 = rgba(options.color);
    p.color2 = {options.brush.hardness,
                tip_size ? float(std::max(tip_size->width, tip_size->height)) / 2 : 0, 0, 0};
    for (std::size_t first = 0; first < kept.size(); first += chunk) {
        const auto count = std::min(chunk, kept.size() - first);
        auto record = base;
        record.parameters.reserved = {static_cast<std::uint32_t>(count),
                                      static_cast<std::uint32_t>(side),
                                      (options.finger_painting ? 1u : 0u) | (first == 0 ? 2u : 0u) |
                                          (wide ? 4u : 0u) | (options.preserve_alpha ? 8u : 0u),
                                      carried->width};
        const auto words = op.data(record, 8 * count, "samples");
        for (std::size_t i = 0; i < count; ++i) {
            const auto& dab = kept[first + i];
            const auto [shape, gain] = dab_shape(dab);
            const std::array<float, 8> values{dab.center.x, dab.center.y,   shape[0],
                                              shape[1],     shape[2],       shape[3],
                                              dab.opacity,  dab.flow * gain};
            std::ranges::transform(values, words.begin() + std::ptrdiff_t(8 * i),
                                   [](float value) { return std::bit_cast<std::uint32_t>(value); });
        }
        if (wide) {
            for (std::uint32_t y = 0; y < side; y += patch_tile) {
                for (std::uint32_t x = 0; x < side; x += patch_tile) {
                    auto part = record;
                    const auto w = std::min(patch_tile, std::uint32_t(side) - x);
                    const auto h = std::min(patch_tile, std::uint32_t(side) - y);
                    part.parameters.extra = {float(x), float(y), float(w), float(h)};
                    part.workgroups = {(w + 15) / 16, (h + 15) / 16, 1};
                    records.push_back(std::move(part));
                }
            }
        } else {
            records.push_back(std::move(record));
        }
    }
    // Stage every payload before committing any record: failure is atomic.
    for (std::size_t i = 0; i < records.size(); ++i) {
        records[i].starts_batch = i % 32 == 0;
        op.append({records[i]});
    }
}
} // namespace wgpupixel
