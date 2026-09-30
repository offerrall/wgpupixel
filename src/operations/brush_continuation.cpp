#include "brush_engine.h"
#include <algorithm>
#include <map>
#include <new>

namespace wgpupixel::detail {
struct StrokeState {
    std::shared_ptr<Resource> destination, snapshot;
    std::optional<Rect> bounds;
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
    state->bounds.reset();
    state->tool = {};
    state->valid.reset();
}

Rect unite(Rect a, Rect b) {
    const auto left = std::min(a.x, b.x), top = std::min(a.y, b.y);
    return {left, top, std::max(a.x + a.width, b.x + b.width) - left,
            std::max(a.y + a.height, b.y + b.height) - top};
}

// Replay already has conservative write bounds clipped to the canvas and
// selection region. Smudge dispatches over its carried patch, not canvas pixels;
// its offsets hold the write bounds instead. Reuse these after validation so dab
// placement, including a first dab whose heading changes, is evaluated only once.
Rect replay_bounds(const Record& record, bool smudge) {
    const auto& p = record.parameters;
    if (smudge) {
        return {p.offsets[0], p.offsets[1], p.offsets[2] - p.offsets[0],
                p.offsets[3] - p.offsets[1]};
    }
    return {std::int32_t(p.dispatch[0]), std::int32_t(p.dispatch[1]),
            std::int32_t(p.dispatch[2]), std::int32_t(p.dispatch[3])};
}

Record snapshot_record(const std::shared_ptr<Resource>& source,
                       const std::shared_ptr<Resource>& destination, Rect bounds,
                       const std::shared_ptr<RecordingGuard>& guard) {
    auto record = kernel_record(source->kind == ResourceKind::mask ? Kernel::stroke_copy_mask
                                                                  : Kernel::copy_image,
                                source, destination);
    record.parameters.dispatch = {std::uint32_t(bounds.x), std::uint32_t(bounds.y),
                                  std::uint32_t(bounds.width), std::uint32_t(bounds.height)};
    if (source->kind == ResourceKind::mask && bounds.width > 0 && bounds.height > 0) {
        // One lane per packed word, allowing up to three bytes of row-start
        // misalignment. Keep dispatch in pixels for the shader's exact edge masks.
        const auto words = (std::uint32_t(bounds.width) + 6) / 4;
        record.workgroups = {(words + 7) / 8, (std::uint32_t(bounds.height) + 7) / 8, 1};
    }
    record.recording_guard = guard;
    return record;
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
std::optional<Rect> BrushStrokeState::snapshot_bounds() const noexcept {
    return state_ && state_->valid && *state_->valid ? state_->bounds : std::nullopt;
}
std::optional<Rect> SmudgeStrokeState::snapshot_bounds() const noexcept {
    return state_ && state_->valid && *state_->valid ? state_->bounds : std::nullopt;
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
    for (const auto& item : replay.recording_->records) {
        const auto bounds = replay_bounds(item, tool == "smudge_stroke");
        candidate->bounds = candidate->bounds ? unite(*candidate->bounds, bounds) : bounds;
    }
    {
        Operation op(recording_.get(), tool);
        op.reserve(5 + replay.recording_->records.size());
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
        const auto capture = [&](Rect bounds) {
            if (bounds.width > 0 && bounds.height > 0) {
                op.append({snapshot_record(destination, candidate->snapshot, bounds, guard)});
            }
        };
        if (candidate->bounds) {
            const auto b = *candidate->bounds;
            if (state->bounds) {
                const auto old = *state->bounds;
                // Four disjoint strips of B_new \\ B_old, captured before any dabs.
                capture({b.x, b.y, b.width, old.y - b.y});
                capture({b.x, old.y + old.height, b.width,
                         b.y + b.height - old.y - old.height});
                capture({b.x, old.y, old.x - b.x, old.height});
                capture({old.x + old.width, old.y,
                         b.x + b.width - old.x - old.width, old.height});
                // Newly captured pixels are still untouched in the destination.
                // Restoring only B_old leaves all of B_new ready for exact replay.
                op.append({snapshot_record(candidate->snapshot, destination, old, guard)});
            } else {
                capture(b);
            }
        } else {
            // Even an empty/off-canvas prefix is a dependency (spacing and pigment).
            // Keep the discard/failure guard without copying any pixels.
            op.append({snapshot_record(destination, candidate->snapshot, {}, guard)});
        }
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
