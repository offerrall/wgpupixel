#include "utils/operations.h"
#include "filter_pyramid.h"
#include <limits>

namespace wgpupixel {
using namespace detail;

void Commands::gaussian_blur(const Image& destination, const GaussianBlurOptions& options) {
    Operation op(recording_.get(), "gaussian_blur");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& pixels = op.resource(destination.resource_, "destination");
    op.require(options.radius >= 0 && options.radius <= std::numeric_limits<std::int32_t>::max(),
               "radius", "radius must be a nonnegative 32-bit signed integer");
    op.require(std::isfinite(options.sigma) && options.sigma > 0.0f, "sigma",
               "sigma must be finite and positive");
    op.workspace(options.workspace.storage_,
                 gaussian_blur_requirements({pixels->width, pixels->height}, options).workspace);
    if (options.radius == 0) {
        return;
    }
    const double effective =
        std::min(double(options.radius), std::floor(double(options.sigma) * 16));
    if (effective > 128) {
        const auto records = large_gaussian(op, pixels, pixels, options.sigma, options.radius);
        append_filter_records(op, records, options.region);
        return;
    }
    auto work = op.temporary(pixels->width, pixels->height);
    auto horizontal = kernel_record(Kernel::blur_horizontal, pixels, work);
    horizontal.parameters.values[0] = options.sigma;
    horizontal.parameters.offsets[2] = static_cast<std::int32_t>(options.radius);
    auto vertical = kernel_record(Kernel::blur_vertical, work, pixels);
    vertical.parameters.values = horizontal.parameters.values;
    vertical.parameters.offsets = horizontal.parameters.offsets;
    const auto support = std::min(double(options.radius), std::floor(double(options.sigma) * 16));
    // The tiled short axis emits four pixels per workgroup. Keep unusually
    // long, thin images within WebGPU's 65535-workgroup dispatch limit.
    if (support <= 64 && pixels->width <= 262140 && pixels->height <= 262140) {
        horizontal.kernel = vertical.kernel = Kernel::gaussian_tiled;
        horizontal.parameters.offsets = {0, 0, static_cast<std::int32_t>(support), 0};
        vertical.parameters.offsets = {0, 1, static_cast<std::int32_t>(support), 0};
        const auto w = pixels->width, h = pixels->height;
        horizontal.parameters.dispatch = {0, 0, ((w + 15) / 16) * 8, ((h + 3) / 4) * 8};
        vertical.parameters.dispatch = {0, 0, ((w + 3) / 4) * 8, ((h + 15) / 16) * 8};
        vertical.parameters.reserved = {0, 0, w, h};
        if (options.region) {
            const auto area = *options.region;
            vertical.parameters.reserved = {
                static_cast<std::uint32_t>(std::clamp<std::int64_t>(area.x, 0, w)),
                static_cast<std::uint32_t>(std::clamp<std::int64_t>(area.y, 0, h)),
                static_cast<std::uint32_t>(
                    std::clamp<std::int64_t>(std::int64_t(area.x) + area.width, 0, w)),
                static_cast<std::uint32_t>(
                    std::clamp<std::int64_t>(std::int64_t(area.y) + area.height, 0, h))};
        }
        if (vertical.parameters.reserved[0] >= vertical.parameters.reserved[2] ||
            vertical.parameters.reserved[1] >= vertical.parameters.reserved[3]) {
            return;
        }
        filter_weights(op, horizontal, vertical, options.sigma, static_cast<std::int64_t>(support));
        op.region(nullptr);
    }
    op.append({horizontal, vertical});
}
} // namespace wgpupixel
