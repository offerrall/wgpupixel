#include "brush_engine.h"

namespace wgpupixel {
using namespace detail;
namespace {
void mask_stroke(Recording* recording, const std::shared_ptr<Resource>& destination,
                 const std::shared_ptr<Resource>& selection, bool selected,
                 const std::shared_ptr<Resource>& tip, const MaskBrushStrokeOptions& options,
                 std::string_view name) {
    Operation op(recording, name);
    const auto& pixels = op.resource(destination, "destination", ResourceKind::mask);
    op.selection(selection, pixels, selected, options.region);
    if (selected) {
        op.distinct(*selection, *pixels, "mask");
    }
    if (options.brush.tip) {
        op.resource(tip, "brush.tip", ResourceKind::mask);
        op.distinct(*tip, *pixels, "brush.tip");
    }
    require_unit(op, options.coverage, "coverage");
    require_unit(op, options.opacity, "opacity");
    require_unit(op, options.flow, "flow");
    auto record = stroke_record(
        op, {options.samples, options.brush, tip.get(), options.opacity, options.flow, name},
        tip ? kernel_record(Kernel::brush_mask_tip, tip, pixels)
            : kernel_record(Kernel::brush_mask_paint, pixels),
        options.region);
    if (!record) {
        return;
    }
    record->parameters.values[0] = options.coverage;
    append_stroke(op, *record);
}
} // namespace
void Commands::brush_stroke(const Mask& destination, const MaskBrushStrokeOptions& options) {
    mask_stroke(recording_.get(), destination.resource_,
                options.mask ? options.mask->resource_ : nullptr, options.mask != nullptr,
                options.brush.tip ? options.brush.tip->resource_ : nullptr, options,
                "brush_stroke");
}
void Commands::eraser_stroke(const Mask& destination, const EraserStrokeOptions& options) {
    MaskBrushStrokeOptions stroke{options.samples, options.brush, 0, options.opacity, options.flow,
                                  options.mask,    options.region};
    mask_stroke(recording_.get(), destination.resource_,
                options.mask ? options.mask->resource_ : nullptr, options.mask != nullptr,
                options.brush.tip ? options.brush.tip->resource_ : nullptr, stroke,
                "eraser_stroke");
}
} // namespace wgpupixel
