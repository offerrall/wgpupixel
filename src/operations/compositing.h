#pragma once
#include "utils/operations.h"
#include <algorithm>

namespace wgpupixel::detail {
inline void require_blend_if(const Operation& op, const std::optional<BlendIfOptions>& options) {
    if (!options) {
        return;
    }
    op.require(std::to_underlying(options->encoding) <= std::to_underlying(ColorEncoding::srgb),
               "blend_if.encoding", "unknown Blend If encoding");
    for (const auto r : {options->source, options->destination}) {
        op.require(std::isfinite(r.black) && std::isfinite(r.black_split) &&
                       std::isfinite(r.white_split) && std::isfinite(r.white) && r.black >= 0 &&
                       r.black <= r.black_split && r.black_split <= r.white_split &&
                       r.white_split <= r.white && r.white <= 1,
                   "blend_if", "split sliders must be finite, ordered and between zero and one");
    }
}

inline std::uint32_t blend_seed(Recording& recording, const Resource& source, BlendMode mode,
                                std::optional<std::uint32_t> seed) {
    if (mode != BlendMode::dissolve) {
        return 0;
    }
    if (seed) {
        return *seed;
    }
    // Resource identity survives edits; recording-local order distinguishes duplicates.
    auto key = std::uint64_t(reinterpret_cast<std::uintptr_t>(&source));
    key += 0x9e3779b97f4a7c15ull * (std::uint64_t(recording.next_dissolve_index++) + 1);
    key = (key ^ (key >> 30)) * 0xbf58476d1ce4e5b9ull;
    key = (key ^ (key >> 27)) * 0x94d049bb133111ebull;
    key ^= key >> 31;
    return static_cast<std::uint32_t>(key ^ (key >> 32));
}

template <class Options>
Record layer_record(const std::shared_ptr<Resource>& source,
                    const std::shared_ptr<Resource>& destination,
                    const std::shared_ptr<Resource>& mask, Position position, float opacity,
                    BlendMode mode, const Options& options, std::uint32_t seed) {
    auto record = kernel_record(Kernel::blend_layer, source, destination);
    record.sources[0] = mask ? mask : source;
    auto& p = record.parameters;
    p.values[0] = opacity;
    p.offsets[0] = position.x;
    p.offsets[1] = position.y;
    p.reserved = {
        std::to_underlying(mode), mask ? 1u : 0u,
        (options.preserve_alpha ? 1u : 0u) | (options.blend_if ? 4u : 0u) |
            (options.blend_if && options.blend_if->encoding == ColorEncoding::srgb ? 8u : 0u),
        seed};
    if (options.blend_if) {
        const auto a = options.blend_if->source, b = options.blend_if->destination;
        p.color1 = {a.black, a.black_split, a.white_split, a.white};
        p.color2 = {b.black, b.black_split, b.white_split, b.white};
    }
    const auto left = std::max<std::int64_t>(0, position.x);
    const auto top = std::max<std::int64_t>(0, position.y);
    const auto right =
        std::min<std::int64_t>(destination->width, std::int64_t(position.x) + source->width);
    const auto bottom =
        std::min<std::int64_t>(destination->height, std::int64_t(position.y) + source->height);
    p.dispatch = {static_cast<std::uint32_t>(left), static_cast<std::uint32_t>(top),
                  static_cast<std::uint32_t>(std::max<std::int64_t>(0, right - left)),
                  static_cast<std::uint32_t>(std::max<std::int64_t>(0, bottom - top))};
    return record;
}
} // namespace wgpupixel::detail
