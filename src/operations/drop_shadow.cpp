#include "utils/operations.h"
#include "utils/sizes.h"

namespace wgpupixel {
using namespace detail;
void Commands::drop_shadow(const Image& source, const Image& destination,
                           const DropShadowOptions& options) {
    Operation op(recording_.get(), "drop_shadow");
    const auto& src = op.resource(source.resource_, "source");
    const auto& dst = op.resource(destination.resource_, "destination");
    op.distinct(*src, *dst);
    const auto geometry = shadow_geometry({src->width, src->height}, options, "drop_shadow");
    const auto& sizes = geometry.requirements;
    op.require(dst->width == sizes.destination.width && dst->height == sizes.destination.height,
               "destination", "destination dimensions do not match expansion policy");
    op.workspace(
        options.workspace.storage_,
        drop_shadow_requirements({source.resource_->width, source.resource_->height}, options)
            .workspace);
    auto mask = op.temporary(sizes.shadow.width, sizes.shadow.height);
    auto work = options.radius ? op.temporary(sizes.shadow.width, sizes.shadow.height) : mask;
    auto tinted = kernel_record(Kernel::drop_shadow, src, mask);
    tinted.parameters.offsets[2] = static_cast<std::int32_t>(options.radius);
    tinted.parameters.color1 = rgba(options.color);
    auto horizontal = kernel_record(Kernel::blur_horizontal, mask, work);
    horizontal.parameters.values[0] = options.sigma;
    horizontal.parameters.offsets[2] = static_cast<std::int32_t>(options.radius);
    auto vertical = kernel_record(Kernel::blur_vertical, work, mask);
    vertical.parameters.values = horizontal.parameters.values;
    vertical.parameters.offsets = horizontal.parameters.offsets;
    auto clear = kernel_record(Kernel::fill, dst);
    auto backdrop = kernel_record(Kernel::blend, mask, dst);
    backdrop.parameters.values[0] = 1;
    backdrop.parameters.offsets[0] = geometry.shadow_position.x;
    backdrop.parameters.offsets[1] = geometry.shadow_position.y;
    auto foreground = kernel_record(Kernel::blend, src, dst);
    foreground.parameters.values[0] = 1;
    foreground.parameters.offsets[0] = geometry.source_position.x;
    foreground.parameters.offsets[1] = geometry.source_position.y;
    if (options.radius == 0) {
        op.append({tinted, clear, backdrop, foreground});
    } else {
        op.append({tinted, horizontal, vertical, clear, backdrop, foreground});
    }
}
} // namespace wgpupixel
