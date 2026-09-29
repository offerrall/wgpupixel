#pragma once
#include "../runtime.h"
#include <initializer_list>
#include <algorithm>
#include <new>

namespace wgpupixel::detail {
struct WorkspaceAccess {
    static auto& slots(WorkspacePlan& plan) { return plan.slots_; }
    static const auto& slots(const WorkspacePlan& plan) { return plan.slots_; }
};
// Append one buffer, rounded to WebGPU's four-byte allocation granularity.
// Zero-size requests consume no slot.
inline void workspace_buffer(WorkspacePlan& plan, std::uint64_t bytes,
                               std::string_view operation = "workspace_requirements") {
    if (!bytes) return;
    if (bytes > UINT64_MAX - 3 || mask_bytes(bytes) > UINT64_MAX - plan.bytes())
        throw Error(ErrorCode::capacity, operation, "workspace", "workspace size overflows uint64");
    try {
        auto& slots = WorkspaceAccess::slots(plan);
        const auto aligned = mask_bytes(bytes);
        slots.insert(std::lower_bound(slots.begin(), slots.end(), aligned, std::greater<>{}), aligned);
    } catch (const std::bad_alloc&) {
        throw Error(ErrorCode::out_of_memory, operation, "workspace", "host allocation failed");
    }
}
inline WorkspacePlan workspace_plan(std::initializer_list<std::uint64_t> bytes,
                                      std::string_view operation = "workspace_requirements") {
    WorkspacePlan result;
    for (const auto count : bytes) workspace_buffer(result, count, operation);
    return result;
}
} // namespace wgpupixel::detail
