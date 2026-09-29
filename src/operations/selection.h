#pragma once
#include "../utils/operations.h"
#include "../utils/workspace.h"
#include <vector>

namespace wgpupixel::detail {
// Selection kernels index a linear range of invocations, 64 per 8x8 workgroup.
inline std::array<std::uint32_t, 4> linear_dispatch(std::uint64_t invocations) {
    const auto groups = std::max<std::uint64_t>((invocations + 63) / 64, 1);
    const auto columns = std::min<std::uint64_t>(groups, 32768);
    const auto rows = (groups + columns - 1) / columns;
    return {0, 0, static_cast<std::uint32_t>(columns * 8), static_cast<std::uint32_t>(rows * 8)};
}
inline std::uint64_t mask_words(const Resource& mask) {
    return (std::uint64_t(mask.width) * mask.height + 3) / 4;
}
// Packed mask kernels clip each byte, preserving neighboring bytes in the same word.
inline void mask_write(Operation& op, Record& record, const std::shared_ptr<Resource>& selection,
                       const std::optional<Rect>& region) {
    const auto& target = record.destination ? record.destination : record.source;
    op.coverage(selection, target, bool(selection));
    if (selection) {
        op.distinct(*selection, *target, "mask");
    }
    const auto width = std::int64_t(target->width), height = std::int64_t(target->height);
    record.parameters.offsets = {0, 0, std::int32_t(width), std::int32_t(height)};
    if (region) {
        op.require(region->width >= 0 && region->height >= 0, "region",
                   "region dimensions must be nonnegative");
        record.parameters.offsets = {std::int32_t(std::clamp<std::int64_t>(region->x, 0, width)),
                                     std::int32_t(std::clamp<std::int64_t>(region->y, 0, height)),
                                     std::int32_t(std::clamp<std::int64_t>(
                                         std::int64_t(region->x) + region->width, 0, width)),
                                     std::int32_t(std::clamp<std::int64_t>(
                                         std::int64_t(region->y) + region->height, 0, height))};
    }
    record.parameters.dispatch = linear_dispatch(mask_words(*target));
}
inline void require_mode(const Operation& op, SelectionMode mode) {
    op.require(std::to_underlying(mode) <= std::to_underlying(SelectionMode::difference), "mode",
               "unknown selection mode");
}
// Selection scratch is one packed plane of width x rows coverage bytes; zero rows
// need no workspace.
inline WorkspacePlan scratch_plan(ImageSize scratch, std::string_view operation) {
    WorkspacePlan plan;
    workspace_buffer(plan, mask_bytes(std::uint64_t(scratch.width) * scratch.height), operation);
    return plan;
}
// Checks workspace against the scratch plane and returns it as a mask view, or null
// when no scratch is needed.
inline std::shared_ptr<Resource> scratch_view(Operation& op,
                                              const std::shared_ptr<WorkspaceStorage>& workspace,
                                              ImageSize scratch) {
    op.workspace(workspace, scratch_plan(scratch, op.name()));
    if (scratch.width == 0 || scratch.height == 0) {
        return nullptr;
    }
    return op.temporary(static_cast<std::uint32_t>(scratch.width),
                        static_cast<std::uint32_t>(scratch.height), ResourceKind::mask);
}
inline ImageSize size_of(const Resource& resource) {
    return {resource.width, resource.height};
}
// Feather and smooth scratch: the coverage plane, then 16-bit sums from this word.
inline std::uint32_t filter_sums_offset(ImageSize mask) {
    return static_cast<std::uint32_t>((mask.width * mask.height + 3) / 4);
}
// The separable passes filtering the scratch coverage plane into destination.
// kind 0: Gaussian with weights for distances 0..radius; kind 1: box majority.
std::array<Record, 2> mask_filter_records(Operation& op, const std::shared_ptr<Resource>& scratch,
                                          const std::shared_ptr<Resource>& destination,
                                          std::uint32_t kind, std::span<const float> weights,
                                          std::uint32_t radius, bool canvas_bounds,
                                          SelectionMode mode);
// Scratch plane rows of feather (and of feathered shapes) for a mask size.
ImageSize feather_scratch(ImageSize mask, std::string_view operation);
// Normalized Gaussian weights for distances 0..ceil(3 radius); radius is sigma.
std::vector<float> feather_weights(double radius);
// Feather passes over the scratch coverage plane into destination: an exact Gaussian
// for radii up to fir_taps / 3, else box_passes running-sum boxes per axis.
inline constexpr double fir_taps = 48;
inline constexpr std::int64_t box_passes = 6;
std::size_t feather_passes(double radius);
std::vector<Record> feather_records(Operation& op, const std::shared_ptr<Resource>& scratch,
                                    const std::shared_ptr<Resource>& destination, double radius,
                                    bool canvas_bounds, SelectionMode mode);
} // namespace wgpupixel::detail
