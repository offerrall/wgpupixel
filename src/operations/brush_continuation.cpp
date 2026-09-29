#include "brush_engine.h"
#include <algorithm>
#include <map>
#include <new>

namespace wgpupixel::detail {
struct StrokeState {
    std::shared_ptr<Resource> destination, snapshot;
    std::vector<StrokeSample> samples;
    std::string_view tool;
    // Every committed recording of this stroke shares its validity. Dropping any
    // dependency invalidates later continuations, including ones already recorded.
    std::shared_ptr<bool> valid;
};
} // namespace wgpupixel::detail
namespace wgpupixel {
using namespace detail;

namespace {
std::shared_ptr<StrokeState> reserve_stroke_state(const std::shared_ptr<State>& context,
                                                  const std::shared_ptr<Resource>& destination,
                                                  ResourceKind kind,
                                                  std::string_view operation) try {
    auto owner = require_state(context, operation);
    std::lock_guard lock(owner->mutex);
    owner->check(operation);
    validate_resource(owner, destination, kind, operation, "destination");
    auto state = std::make_shared<StrokeState>();
    auto snapshot = std::make_shared<Resource>();
    snapshot->owner = owner;
    snapshot->kind = kind;
    snapshot->width = destination->width;
    snapshot->height = destination->height;
    snapshot->capacity = std::uint64_t(snapshot->width) * snapshot->height;
    snapshot->element_bytes = kind == ResourceKind::mask ? 1 : pixel_bytes;
    snapshot->buffer = allocate_buffer(
        *owner, mask_bytes(snapshot->capacity * snapshot->element_bytes),
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst, operation,
        kind == ResourceKind::mask ? MemoryCategory::masks : MemoryCategory::images);
    owner->register_resource(snapshot);
    state->snapshot = std::move(snapshot);
    return state;
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, operation, "state", "host allocation failed");
}
void reset_stroke(const std::shared_ptr<StrokeState>& state) noexcept {
    if (!state) {
        return;
    }
    state->destination.reset();
    state->samples.clear();
    state->tool = {};
    state->valid.reset();
}
} // namespace

BrushStrokeState Context::create_brush_stroke_state(const Image& destination) {
    BrushStrokeState result;
    result.state_ = reserve_stroke_state(state_, destination.resource_, ResourceKind::image,
                                         "create_brush_stroke_state");
    return result;
}
BrushStrokeState Context::create_brush_stroke_state(const Mask& destination) {
    BrushStrokeState result;
    result.state_ = reserve_stroke_state(state_, destination.resource_, ResourceKind::mask,
                                         "create_brush_stroke_state");
    return result;
}
SmudgeStrokeState Context::create_smudge_stroke_state(const Image& destination) {
    SmudgeStrokeState result;
    result.state_ = reserve_stroke_state(state_, destination.resource_, ResourceKind::image,
                                         "create_smudge_stroke_state");
    return result;
}
void BrushStrokeState::reset() noexcept {
    reset_stroke(state_);
}
void SmudgeStrokeState::reset() noexcept {
    reset_stroke(state_);
}

void Commands::continue_stroke(
    const std::shared_ptr<Resource>& destination, std::shared_ptr<StrokeState>& state,
    std::span<const StrokeSample> samples, std::string_view tool,
    const std::function<void(Commands&, std::span<const StrokeSample>)>& record) {
    auto candidate = std::make_shared<StrokeState>();
    bool continuing = false;
    {
        Operation op(recording_.get(), tool);
        const auto kind = destination && destination->kind == ResourceKind::mask
                              ? ResourceKind::mask
                              : ResourceKind::image;
        op.resource(destination, "destination", kind);
        op.require(bool(state), "state", "state has no reserved snapshot",
                   ErrorCode::invalid_resource);
        op.resource(state->snapshot, "state", kind);
        op.same_size(*state->snapshot, *destination, "destination");
        continuing = !state->tool.empty();
        if (continuing) {
            op.require(state->valid && *state->valid, "state",
                       "stroke recording was discarded or failed; reset the state before reuse",
                       ErrorCode::invalid_resource);
            op.require(state->destination == destination && state->tool == tool, "state",
                       "state belongs to a different destination or tool");
        }
        *candidate = *state;
        candidate->destination = destination;
        candidate->tool = tool;
    }
    if (!continuing) {
        candidate->valid = std::make_shared<bool>(true);
    }
    auto guard = std::make_shared<RecordingGuard>();
    guard->valid = candidate->valid;
    candidate->samples.insert(candidate->samples.end(), samples.begin(), samples.end());
    // Build the replay separately. No destination record or state is committed until
    // all validation, allocations and data staging have succeeded.
    auto pending = std::make_unique<Recording>();
    pending->owner = recording_->owner;
    pending->capacity = recording_->capacity;
    // Replay uses the shared growable recorder but is never submitted. Retain
    // the current uniform allocation until replay needs its own larger storage.
    pending->stride = recording_->stride;
    pending->parameters.resize(pending->capacity * pending->stride);
    pending->uniform = recording_->uniform.retain();
    pending->records.reserve(pending->capacity);
    Commands replay(std::move(pending));
    record(replay, candidate->samples);
    {
        Operation op(recording_.get(), tool);
        op.reserve(1 + replay.recording_->records.size());
        auto records = replay.recording_->records;
        std::map<std::uint64_t, std::uint64_t> staged;
        for (auto& item : records) {
            if (item.data_offset != no_data) {
                const auto offset = item.data_offset, count = item.data_count;
                if (const auto found = staged.find(offset); found != staged.end()) {
                    item.data_offset = found->second;
                } else {
                    item.data_offset = no_data;
                    op.data(item,
                            std::span<const std::uint32_t>(replay.recording_->data)
                                .subspan(offset, count),
                            "samples");
                    staged.emplace(offset, item.data_offset);
                }
            }
        }
        auto snapshot = continuing ? copy_record(candidate->snapshot, destination)
                                   : copy_record(destination, candidate->snapshot);
        snapshot.bytes = candidate->snapshot->capacity *
                         (destination->kind == ResourceKind::mask ? 1 : pixel_bytes);
        snapshot.recording_guard = guard;
        op.append({snapshot});
        for (const auto& item : records) {
            // Restore each replay record's selection; copy records are unselected.
            op.coverage(item.coverage, destination, item.coverage != nullptr);
            op.append({item});
        }
        // Rollback before this point must preserve an earlier valid continuation.
        guard->committed = true;
        state = std::move(candidate);
    }
}

void Commands::brush_stroke(const Image& destination, BrushStrokeState& state,
                            const BrushStrokeOptions& options) {
    continue_stroke(destination.resource_, state.state_, options.samples, "brush_stroke",
                    [&](Commands& replay, std::span<const StrokeSample> samples) {
                        auto all = options;
                        all.samples = samples;
                        replay.brush_stroke(destination, all);
                    });
}
void Commands::eraser_stroke(const Image& destination, BrushStrokeState& state,
                             const EraserStrokeOptions& options) {
    continue_stroke(destination.resource_, state.state_, options.samples, "eraser_stroke",
                    [&](Commands& replay, std::span<const StrokeSample> samples) {
                        auto all = options;
                        all.samples = samples;
                        replay.eraser_stroke(destination, all);
                    });
}
void Commands::brush_stroke(const Mask& destination, BrushStrokeState& state,
                            const MaskBrushStrokeOptions& options) {
    continue_stroke(destination.resource_, state.state_, options.samples, "brush_stroke",
                    [&](Commands& replay, std::span<const StrokeSample> samples) {
                        auto all = options;
                        all.samples = samples;
                        replay.brush_stroke(destination, all);
                    });
}
void Commands::eraser_stroke(const Mask& destination, BrushStrokeState& state,
                             const EraserStrokeOptions& options) {
    continue_stroke(destination.resource_, state.state_, options.samples, "eraser_stroke",
                    [&](Commands& replay, std::span<const StrokeSample> samples) {
                        auto all = options;
                        all.samples = samples;
                        replay.eraser_stroke(destination, all);
                    });
}
void Commands::smudge_stroke(const Image& destination, SmudgeStrokeState& state,
                             const SmudgeStrokeOptions& options) {
    continue_stroke(destination.resource_, state.state_, options.samples, "smudge_stroke",
                    [&](Commands& replay, std::span<const StrokeSample> samples) {
                        auto all = options;
                        all.samples = samples;
                        replay.smudge_stroke(destination, all);
                    });
}
} // namespace wgpupixel
