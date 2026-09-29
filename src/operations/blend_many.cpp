#include "compositing.h"
#include <algorithm>
#include <bit>

namespace wgpupixel {
using namespace detail;

void Commands::blend_many(std::span<const Image> sources, const Image& destination,
                          const BlendManyOptions& options) {
    Operation op(recording_.get(), "blend_many");
    const auto& dst = op.resource(destination.resource_, "destination");
    op.selection(options.mask ? options.mask->resource_ : nullptr, dst, options.mask != nullptr,
                 options.region);
    op.require(options.positions.size() == sources.size(), "positions",
               "one position is required per source");
    op.require(options.opacities.empty() || options.opacities.size() == sources.size(), "opacities",
               "length must match sources");
    op.require(options.modes.empty() || options.modes.size() == sources.size(), "modes",
               "length must match sources");
    op.require(options.layers.empty() || options.layers.size() == sources.size(), "layers",
               "length must match sources");
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const auto& source = op.resource(sources[i].resource_, "sources");
        op.distinct(*source, *dst, "sources");
        if (!options.layers.empty()) {
            const auto& layer = options.layers[i];
            require_blend_if(op, layer.blend_if);
            if (layer.source_mask) {
                const auto& mask =
                    op.resource(layer.source_mask->resource_, "source_mask", ResourceKind::mask);
                op.same_size(*mask, *source, "source_mask");
            }
        }
        if (!options.opacities.empty()) {
            require_finite(op, options.opacities[i], "opacities");
            op.require(options.opacities[i] >= 0 && options.opacities[i] <= 1, "opacities",
                       "opacity must be between zero and one");
        }
        if (!options.modes.empty()) {
            op.require(std::to_underlying(options.modes[i]) <=
                           std::to_underlying(BlendMode::dissolve),
                       "modes", "blend mode is invalid");
        }
    }
    const bool advanced = std::ranges::any_of(options.layers, [](const auto& layer) {
        return layer.source_mask || layer.preserve_alpha || layer.blend_if.has_value();
    });
    if (advanced) {
        op.reserve(sources.size());
        for (std::size_t i = 0; i < sources.size(); ++i) {
            const auto& layer = options.layers[i];
            const auto mode = options.modes.empty() ? BlendMode::normal : options.modes[i];
            const auto seed = blend_seed(*recording_, *sources[i].resource_, mode, layer.seed);
            auto record = layer_record(
                sources[i].resource_, dst,
                layer.source_mask ? layer.source_mask->resource_ : nullptr, options.positions[i],
                options.opacities.empty() ? 1.0f : options.opacities[i], mode, layer, seed);
            op.append({record});
        }
        return;
    }
    constexpr std::size_t batch_size = 4;
    op.reserve(sources.size() / batch_size + (sources.size() % batch_size != 0));
    for (std::size_t first = 0; first < sources.size(); first += batch_size) {
        const auto count = std::min(batch_size, sources.size() - first);
        auto record = kernel_record(Kernel::blend_many, sources[first].resource_, dst);
        auto& p = record.parameters;
        std::int64_t left = dst->width, top = dst->height, right = 0, bottom = 0;
        for (std::size_t j = 0; j < batch_size; ++j) {
            const auto i = first + std::min(j, count - 1);
            const auto& source = sources[i].resource_;
            const auto position = options.positions[i];
            if (j > 0) {
                record.sources[j - 1] = source;
            }
            p.values[j] = options.opacities.empty() ? 1.0f : options.opacities[i];
            p.reserved[j] = options.modes.empty() ? 0 : std::to_underlying(options.modes[i]);
            if (j < count) {
                const auto seed =
                    blend_seed(*recording_, *source, static_cast<BlendMode>(p.reserved[j]),
                               options.layers.empty() ? std::nullopt : options.layers[i].seed);
                p.extra2[j] = std::bit_cast<float>(seed);
            }
            if (j < 2) {
                p.offsets[2 * j] = position.x;
                p.offsets[2 * j + 1] = position.y;
            } else {
                p.extra[2 * (j - 2)] = std::bit_cast<float>(position.x);
                p.extra[2 * (j - 2) + 1] = std::bit_cast<float>(position.y);
            }
            if (j == 1 || j == 2) {
                p.color1[2 * (j - 1)] = std::bit_cast<float>(source->width);
                p.color1[2 * (j - 1) + 1] = std::bit_cast<float>(source->height);
            } else if (j == 3) {
                p.color2[0] = std::bit_cast<float>(source->width);
                p.color2[1] = std::bit_cast<float>(source->height);
            }
            left = std::min(left, std::int64_t(position.x));
            top = std::min(top, std::int64_t(position.y));
            right = std::max(right, std::int64_t(position.x) + source->width);
            bottom = std::max(bottom, std::int64_t(position.y) + source->height);
        }
        left = std::max<std::int64_t>(0, left);
        top = std::max<std::int64_t>(0, top);
        right = std::min<std::int64_t>(dst->width, right);
        bottom = std::min<std::int64_t>(dst->height, bottom);
        if (left >= right || top >= bottom) {
            continue;
        }
        p.color2[2] = std::bit_cast<float>(static_cast<std::uint32_t>(count));
        p.dispatch = {static_cast<std::uint32_t>(left), static_cast<std::uint32_t>(top),
                      static_cast<std::uint32_t>(right - left),
                      static_cast<std::uint32_t>(bottom - top)};
        op.append({record});
    }
}
} // namespace wgpupixel
