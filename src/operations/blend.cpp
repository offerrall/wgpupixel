#include "compositing.h"

namespace wgpupixel {
using namespace detail;

void Commands::blend(const Image& source, const Image& destination, const BlendOptions& options) {
    Operation op(recording_.get(), "blend");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& src = op.resource(source.resource_, "source");
    const auto& dst = op.resource(destination.resource_, "destination");
    op.distinct(*src, *dst);
    op.require(std::isfinite(options.opacity) && options.opacity >= 0.0f && options.opacity <= 1.0f,
               "opacity", "opacity must be finite and between zero and one");
    op.require(std::to_underlying(options.mode) <= std::to_underlying(BlendMode::dissolve), "mode",
               "unknown blend mode");
    require_blend_if(op, options.blend_if);
    std::shared_ptr<Resource> mask;
    if (options.source_mask) {
        mask = op.resource(options.source_mask->resource_, "source_mask", ResourceKind::mask);
        op.same_size(*mask, *src, "source_mask");
    }
    op.reserve(1);
    const auto seed = blend_seed(*recording_, *src, options.mode, options.seed);
    auto record = layer_record(src, dst, mask, options.position, options.opacity, options.mode,
                               options, seed);
    op.append({record});
}
} // namespace wgpupixel
