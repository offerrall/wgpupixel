#include "utils/commands.h"
#include "utils/workspace.h"
#include <algorithm>
#include <cstdio>
#include <limits>
#include <new>

namespace wgpupixel {
using namespace detail;

std::uint64_t WorkspacePlan::bytes() const noexcept {
    std::uint64_t total = 0;
    for (const auto bytes : slots_) total += bytes;
    return total;
}

void WorkspacePlan::merge(const WorkspacePlan& other) try {
    auto merged = slots_;
    merged.resize(std::max(merged.size(), other.slots_.size()));
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < merged.size(); ++i) {
        if (i < other.slots_.size()) merged[i] = std::max(merged[i], other.slots_[i]);
        if (merged[i] > UINT64_MAX - total)
            fail(ErrorCode::capacity, "merge", "workspace", "workspace size overflows uint64");
        total += merged[i];
    }
    slots_.swap(merged);
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, "merge", "workspace", "host allocation failed");
}

Workspace::Workspace(std::shared_ptr<WorkspaceStorage> storage) : storage_(std::move(storage)) {}

std::uint64_t Workspace::capacity() const {
    if (!storage_) return 0;
    auto state = storage_->owner.lock();
    if (!state)
        fail(ErrorCode::invalid_resource, "capacity", "resource", "resource context no longer exists");
    std::lock_guard lock(state->mutex);
    state->check("capacity");
    if (!storage_->alive)
        fail(ErrorCode::invalid_resource, "capacity", "resource", "workspace was destroyed");
    return storage_->bytes;
}

Workspace Context::create_workspace(const WorkspacePlan& plan) try {
    constexpr std::string_view operation = "create_workspace";
    auto state = require_state(state_, operation);
    std::lock_guard lock(state->mutex);
    state->check(operation);
    auto storage = std::make_shared<WorkspaceStorage>();
    storage->owner = state;
    storage->bytes = plan.bytes();
    const auto& slots = WorkspaceAccess::slots(plan);
    storage->buffers.reserve(slots.size());
    for (const auto bytes : slots) {
        if (bytes > std::min(state->limits.maxBufferSize, state->limits.maxStorageBufferBindingSize))
            fail(ErrorCode::capacity, operation, "workspace", "workspace slot exceeds device buffer limits");
        auto buffer = std::make_shared<Resource>();
        buffer->owner = state;
        buffer->kind = ResourceKind::workspace;
        buffer->capacity = bytes;
        buffer->buffer = allocate_buffer(*state, bytes,
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst,
            operation, MemoryCategory::workspace);
        state->register_resource(buffer);
        storage->buffers.push_back(std::move(buffer));
    }
    return Workspace(std::move(storage));
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, "create_workspace", "workspace", "host allocation failed");
}

void Context::destroy(const Workspace& workspace) {
    constexpr std::string_view operation = "destroy";
    auto state = require_state(state_, operation);
    std::lock_guard lock(state->mutex);
    const auto& storage = workspace.storage_;
    if (!storage || storage->owner.lock() != state || !storage->alive)
        fail(ErrorCode::invalid_resource, operation, "workspace", "workspace belongs to no live context or another context");
    for (const auto& buffer : storage->buffers) {
        if (buffer.use_count() > 1)
            fail(ErrorCode::resource_busy, operation, "workspace", "workspace is recorded or pending on the GPU");
    }
    for (const auto& buffer : storage->buffers) {
        buffer->release();
        buffer->alive = false;
    }
    storage->buffers.clear();
    storage->alive = false;
    std::erase_if(state->resources, [](const auto& entry) { return entry.expired(); });
}

namespace detail {
namespace {
[[noreturn]] void insufficient(std::string_view operation, std::size_t slot,
                                std::uint64_t needed, std::uint64_t available) {
    char message[192];
    std::snprintf(message, sizeof(message), "workspace slot %zu requires %llu bytes; %llu available",
                  slot, static_cast<unsigned long long>(needed), static_cast<unsigned long long>(available));
    fail(ErrorCode::capacity, operation, "workspace", message);
}
}
void Operation::workspace(const std::shared_ptr<WorkspaceStorage>& storage, const WorkspacePlan& plan) try {
    if (storage) {
        require(storage->owner.lock() == recording_.owner && storage->alive, "workspace",
                "workspace belongs to no live context or another context", ErrorCode::invalid_resource);
    }
    const auto& needed = WorkspaceAccess::slots(plan);
    for (std::size_t i = 0; i < needed.size(); ++i) {
        const auto available = storage && i < storage->buffers.size() ? storage->buffers[i]->capacity : 0;
        if (available < needed[i]) insufficient(name_, i, needed[i], available);
    }
    workspace_ = storage;
    workspace_used_.assign(storage ? storage->buffers.size() : 0, false);
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, name_, "workspace", "host allocation failed");
}
std::shared_ptr<Resource> Operation::temporary(std::uint32_t width, std::uint32_t height, ResourceKind kind) try {
    const auto pixels = std::uint64_t(width) * height;
    const auto element = kind == ResourceKind::mask ? 1u : pixel_bytes;
    require(pixels && pixels <= UINT32_MAX, "workspace", "workspace view exceeds pixel capacity", ErrorCode::capacity);
    const auto bytes = mask_bytes(pixels * element);
    auto slot = workspace_used_.size();
    std::uint64_t available = 0;
    for (std::size_t i = workspace_used_.size(); i-- > 0;) {
        if (workspace_used_[i]) continue;
        available = std::max(available, workspace_->buffers[i]->capacity);
        if (workspace_->buffers[i]->capacity >= bytes) {
            slot = i;
            break;
        }
    }
    if (slot == workspace_used_.size()) insufficient(name_, slot, bytes, available);
    workspace_used_[slot] = true;
    const auto& backing = workspace_->buffers[slot];
    auto view = std::make_shared<Resource>();
    view->owner = recording_.owner;
    view->kind = kind;
    view->width = width;
    view->height = height;
    view->capacity = pixels;
    view->element_bytes = element;
    view->backing = backing;
    view->buffer = backing->buffer.retain();
    recording_.owner->register_resource(view);
    return view;
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, name_, "workspace", "host allocation failed");
}
} // namespace detail
} // namespace wgpupixel
