#pragma once

#include "wgpupixel.h"
#include "kernels.h"
#include <webgpu/webgpu.h>
#ifndef __EMSCRIPTEN__
#include <webgpu/wgpu.h>
#endif
#include <array>
#include <atomic>
#include <mutex>
#include <vector>
#include <utility>

namespace wgpupixel::detail {
inline constexpr std::uint64_t pixel_bytes = 4 * sizeof(float);
inline constexpr std::uint32_t transfer_bytes(TransferFormat format) {
    return format == TransferFormat::rgba32_float ? 16 : format == TransferFormat::rgba16 ? 8 : 4;
}
[[noreturn]] void fail(ErrorCode, std::string_view operation, std::string_view parameter,
                       std::string_view message);
WGPUStringView sv(std::string_view) noexcept;

inline constexpr std::uint64_t mask_bytes(std::uint64_t pixels) {
    return (pixels + 3) / 4 * 4;
}
enum class MemoryCategory { images, masks, transfers, internal, presentation, workspace };
// Independent of State's mutex: last owners can disappear outside an API call,
// including after State teardown. Never acquires State's mutex.
struct MemoryLedger {
    std::mutex mutex;
    MemoryUsage usage;
    std::uint64_t limit = 0, reserved = 0;
    void reserve(std::uint64_t, MemoryCategory, std::string_view operation);
    void release(std::uint64_t, MemoryCategory, bool committed) noexcept;
    void commit(std::uint64_t, MemoryCategory) noexcept;
};

template <class T, auto Release> class GpuHandle;
using GpuBuffer = GpuHandle<WGPUBuffer, wgpuBufferRelease>;
using GpuTexture = GpuHandle<WGPUTexture, wgpuTextureRelease>;
GpuBuffer allocate_buffer(State&, std::uint64_t bytes, WGPUBufferUsage,
                          std::string_view operation,
                          MemoryCategory = MemoryCategory::internal);
// Owned presentation textures are single-level, single-sample RGBA8 2D images.
GpuTexture allocate_texture(State&, std::uint32_t width, std::uint32_t height,
                            std::string_view operation);

// One Owner per native allocation. Explicit retain shares that owner without a
// second native reference or ledger entry; the public wrapper remains move-only.
// Construction and native handle adoption are restricted to the two factories.
template <class T, auto Release> class GpuHandle {
    struct Owner {
        std::shared_ptr<MemoryLedger> ledger;
        std::uint64_t bytes;
        MemoryCategory category;
        T handle = nullptr;
        bool committed = false;
        Owner(std::shared_ptr<MemoryLedger> l, std::uint64_t b, MemoryCategory c,
              std::string_view operation) : ledger(std::move(l)), bytes(b), category(c) {
            ledger->reserve(bytes, category, operation);
        }
        Owner(const Owner&) = delete;
        Owner& operator=(const Owner&) = delete;
        void commit() noexcept {
            ledger->commit(bytes, category);
            committed = true;
        }
        ~Owner() {
            if (handle) Release(handle);
            ledger->release(bytes, category, committed);
        }
    };
    std::shared_ptr<Owner> owner_;
    explicit GpuHandle(std::shared_ptr<Owner> owner) : owner_(std::move(owner)) {}
    friend GpuBuffer allocate_buffer(State&, std::uint64_t, WGPUBufferUsage,
                                     std::string_view, MemoryCategory);
    friend GpuTexture allocate_texture(State&, std::uint32_t, std::uint32_t, std::string_view);
  public:
    GpuHandle() noexcept = default;
    GpuHandle(GpuHandle&&) noexcept = default;
    GpuHandle& operator=(GpuHandle&&) noexcept = default;
    GpuHandle(const GpuHandle&) = delete;
    GpuHandle& operator=(const GpuHandle&) = delete;
    operator T() const noexcept { return owner_ ? owner_->handle : nullptr; }
    [[nodiscard]] GpuHandle retain() const noexcept { return GpuHandle(owner_); }
    void reset() noexcept { owner_.reset(); }
};

enum class ResourceKind { image, mask, upload, readback, histogram, statistics, workspace };
// Analysis results: kernels clear, fill and stage record.bytes for readback.
inline bool result_kind(ResourceKind kind) noexcept {
    return kind == ResourceKind::histogram || kind == ResourceKind::statistics;
}
struct Resource {
    std::weak_ptr<State> owner;
    GpuBuffer buffer;
    GpuBuffer readback;
    ResourceKind kind = ResourceKind::image;
    bool alive = true;
    std::uint64_t capacity = 0;
    std::uint32_t element_bytes = 4; // CPU transfer bytes per pixel; 1 for mask coverage.
    TransferFormat format = TransferFormat::rgba8; // Image transfer buffers only.
    std::uint32_t width = 0, height = 0;
    std::size_t recorded = 0, pending = 0;
    std::uint64_t valid_bytes = 0;
    // Workspace views retain their backing slot without allocating GPU storage.
    std::shared_ptr<Resource> backing;
    ~Resource();
    void release() noexcept;
    std::uint64_t revision() const noexcept { return revision_; }
    // Distinguish intervening row-layout changes even when dimensions return to A.
    std::uint64_t size_revision() const noexcept { return size_revision_; }
    void mark_written() noexcept { ++revision_; }
    void set_size(std::uint32_t w, std::uint32_t h) noexcept {
        if (width == w && height == h) return;
        ++size_revision_;
        width = w;
        height = h;
        mark_written();
    }

  private:
    std::uint64_t revision_ = 0, size_revision_ = 0;
};

struct WorkspaceStorage {
    std::weak_ptr<State> owner;
    std::vector<std::shared_ptr<Resource>> buffers;
    std::uint64_t bytes = 0;
    bool alive = true;
};

enum class RecordKind { copy, kernel };
inline constexpr std::uint64_t no_data = ~std::uint64_t{0};
// A continuation depends on every recording that contributes to its snapshot/replay.
// Armed only after the operation has committed its new continuation state.
struct RecordingGuard {
    std::shared_ptr<bool> valid;
    bool committed = false;
    void invalidate() const noexcept {
        if (committed && valid) *valid = false;
    }
};
struct Record {
    RecordKind kind = RecordKind::kernel;
    bool unmasked = false;
    bool starts_batch = false;
    Kernel kernel = Kernel::fill;
    std::shared_ptr<Resource> source, destination, coverage;
    std::array<std::shared_ptr<Resource>, 3> sources{};
    Parameters parameters{};
    std::uint64_t bytes = 0;
    // Slice of Recording::data in 32-bit words, bound at @binding(7) for data kernels.
    std::uint64_t data_offset = no_data, data_count = 0;
    // Explicit workgroup counts for kernels with their own dispatch shape; zero dispatches
    // 8x8 workgroups over parameters.dispatch.
    std::array<std::uint32_t, 3> workgroups{};
    std::shared_ptr<RecordingGuard> recording_guard;
    // Compute/copy records write the destination, or the source for in-place kernels.
    Resource& output() const noexcept { return *(destination ? destination : source); }
};

struct Recording {
    std::shared_ptr<State> owner;
    std::uint32_t next_dissolve_index = 0;
    std::vector<Record> records;
    std::vector<std::byte> parameters;
    GpuBuffer uniform;
    // Kernel data staged for the current records. Submit uploads it into storage, a
    // grow-only buffer reused once the previous submission has retired.
    std::vector<std::uint32_t> data;
    GpuBuffer storage;
    std::uint64_t storage_bytes = 0;
    std::size_t capacity = 0, stride = 0;
    std::shared_ptr<Flight> flight;
    ~Recording();
    void clear() noexcept;
    void rollback(std::size_t record_count, std::size_t data_words,
                  std::uint32_t dissolve_index) noexcept;
    void require_ready(std::string_view operation) const;
    void recycle() noexcept;
    void require_slots(std::size_t count, std::string_view operation);
    void append(Record record) noexcept;
};

struct Flight {
    std::weak_ptr<State> owner;
    std::vector<Record> records;
    GpuBuffer uniform, storage;
    std::vector<GpuBuffer> buffers; // Presentation allocations retained until retirement.
    std::uint64_t index = 0;
    WGPUFuture completion{};
    bool retired = false;
    bool asynchronous_failure = false;
    std::atomic<bool> done{false};
    std::atomic<ErrorCode> error{static_cast<ErrorCode>(0)};
    ~Flight();
};

struct State {
    std::mutex mutex;
    std::shared_ptr<MemoryLedger> ledger = std::make_shared<MemoryLedger>();
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPULimits limits = WGPU_LIMITS_INIT;
    std::array<WGPUComputePipeline, 2 * static_cast<std::size_t>(Kernel::count)> pipelines{};
    std::array<WGPUBindGroupLayout, 2 * static_cast<std::size_t>(Kernel::count)> layouts{};
    std::vector<std::weak_ptr<Resource>> resources;
    // 16-byte storage bound where a kernel declares an unused vec4 source; made once.
    std::shared_ptr<Resource> placeholder;
    void register_resource(const std::shared_ptr<Resource>&);
    std::vector<std::shared_ptr<Flight>> flights;
    std::atomic<ErrorCode> asynchronous_error{static_cast<ErrorCode>(0)};
    std::atomic_flag diagnostic_claimed = ATOMIC_FLAG_INIT;
    std::atomic<ErrorCode> diagnostic_code{static_cast<ErrorCode>(0)};
    char diagnostic[1024]{};
    ~State();
    void report_error(ErrorCode, WGPUStringView) noexcept;
    void check(std::string_view operation) const;
    void retire(const std::shared_ptr<Flight>& flight);
};

std::shared_ptr<State> require_state(const std::shared_ptr<State>&, std::string_view);
std::shared_ptr<State> resource_owner(const std::shared_ptr<Resource>&, std::string_view);
void validate_resource(const std::shared_ptr<State>&, const std::shared_ptr<Resource>&,
                       ResourceKind, std::string_view operation, std::string_view parameter);
std::uint64_t validate_size(const State&, std::int64_t width, std::int64_t height,
                            std::string_view operation,
                            std::uint64_t bytes_per_pixel = pixel_bytes);
WGPUInstance create_instance();
void await_future(State&, WGPUFuture);
void drain(State&, const Flight* = nullptr);
// Maps the resource's readback buffer and copies its first bytes.
void read_mapped(State&, const Resource&, std::span<std::byte>, std::string_view operation);
std::uint64_t submit_queue(State&, WGPUCommandBuffer);

class ErrorScope {
  public:
    explicit ErrorScope(State&);
    ~ErrorScope();
    void finish(std::string_view operation);

  private:
    State& state_;
    bool active_ = true;
};
} // namespace wgpupixel::detail
