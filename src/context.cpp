#include "runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace wgpupixel::detail {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
namespace {
template <class T, auto Release> struct Native {
    T value = nullptr;
    ~Native() {
        if (value) {
            Release(value);
        }
    }
    Native() = default;
    explicit Native(T handle) : value(handle) {}
    Native(Native&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
    Native(const Native&) = delete;
    Native& operator=(const Native&) = delete;
};

struct ScopeResult {
    ErrorCode code = static_cast<ErrorCode>(0);
    char message[1024]{};
};

void pop_scope(State& state, ScopeResult& result) noexcept {
    WGPUPopErrorScopeCallbackInfo info = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
    info.mode = WGPUCallbackMode_AllowSpontaneous;
    info.userdata1 = &result;
    info.callback = [](WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message,
                       void* userdata, void*) noexcept {
        if (status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_NoError) {
            return;
        }
        auto& out = *static_cast<ScopeResult*>(userdata);
        out.code = type == WGPUErrorType_OutOfMemory ? ErrorCode::out_of_memory
                                                     : ErrorCode::execution_failed;
        if (message.data) {
            const auto size =
                message.length == WGPU_STRLEN ? std::strlen(message.data) : message.length;
            const auto count = std::min(size, sizeof(out.message) - 1);
            std::memcpy(out.message, message.data, count);
            out.message[count] = '\0';
        }
    };
    await_future(state, wgpuDevicePopErrorScope(state.device, info));
}

void make_pipeline(State& state, Kernel kernel, Shader shader, bool masked = false) {
    const auto index =
        static_cast<std::size_t>(kernel) + (masked ? static_cast<std::size_t>(Kernel::count) : 0);
    if (state.pipelines[index]) return;
    ErrorScope scope(state);
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = sv(masked ? shader.masked_source : shader.source);
    WGPUShaderModuleDescriptor module_desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    module_desc.nextInChain = &source.chain;
    module_desc.label = sv(shader.label);
    Native<WGPUShaderModule, wgpuShaderModuleRelease> module(
        wgpuDeviceCreateShaderModule(state.device, &module_desc));
    scope.finish(shader.label);
    if (!module.value) {
        fail(ErrorCode::execution_failed, shader.label, {}, "shader compilation failed");
    }

    ErrorScope pipeline_scope(state);
    WGPUComputePipelineDescriptor descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    descriptor.label = sv(shader.label);
    descriptor.compute.module = module.value;
    descriptor.compute.entryPoint = sv("main");
    Native<WGPUComputePipeline, wgpuComputePipelineRelease> pipeline(
        wgpuDeviceCreateComputePipeline(state.device, &descriptor));
    pipeline_scope.finish(shader.label);
    if (!pipeline.value) {
        fail(ErrorCode::execution_failed, shader.label, {}, "pipeline creation failed");
    }
    Native<WGPUBindGroupLayout, wgpuBindGroupLayoutRelease> layout(
        wgpuComputePipelineGetBindGroupLayout(pipeline.value, 0));
    if (!layout.value) {
        fail(ErrorCode::execution_failed, shader.label, {}, "pipeline layout unavailable");
    }
    state.layouts[index] = std::exchange(layout.value, nullptr);
    state.pipelines[index] = std::exchange(pipeline.value, nullptr);
}

void encode_record(State& state, WGPUCommandEncoder encoder, const Recording& recording,
                   const Record& record, std::size_t slot) {
    if (record.kind == RecordKind::copy) {
        const auto bytes = (record.source->kind == ResourceKind::mask ||
                            record.destination->kind == ResourceKind::mask)
                               ? mask_bytes(record.bytes)
                               : record.bytes;
        wgpuCommandEncoderCopyBufferToBuffer(encoder, record.source->buffer, 0,
                                             record.output().buffer, 0, bytes);
        if (record.destination->kind == ResourceKind::readback) {
            wgpuCommandEncoderCopyBufferToBuffer(encoder, record.destination->buffer, 0,
                                                 record.destination->readback, 0, bytes);
        }
        return;
    }
    const auto index = static_cast<std::size_t>(record.kernel) +
                       (record.coverage ? static_cast<std::size_t>(Kernel::count) : 0);
    std::array<WGPUBindGroupEntry, 8> entries{};
    entries[0] = WGPU_BIND_GROUP_ENTRY_INIT;
    entries[0].binding = 0;
    entries[0].buffer = record.destination ? record.source->buffer : record.output().buffer;
    entries[0].size = std::uint64_t(record.parameters.dimensions[0]) *
                      record.parameters.dimensions[1] *
                      (record.kernel == Kernel::upload ? record.source->element_bytes : pixel_bytes);
    if (record.source->kind == ResourceKind::mask || record.kernel == Kernel::mask_upload) {
        entries[0].size = mask_bytes(std::uint64_t(record.parameters.dimensions[0]) *
                                     record.parameters.dimensions[1]);
    }
    std::size_t count = 1;
    if (record.destination) {
        entries[1] = WGPU_BIND_GROUP_ENTRY_INIT;
        entries[1].binding = 1;
        entries[1].buffer = record.output().buffer;
        entries[1].size = std::uint64_t(record.parameters.dimensions[2]) *
                          record.parameters.dimensions[3] *
                          (record.kernel == Kernel::download ? record.destination->element_bytes
                                                             : pixel_bytes);
        if (record.destination->kind == ResourceKind::mask ||
            record.kernel == Kernel::mask_download) {
            entries[1].size = mask_bytes(std::uint64_t(record.parameters.dimensions[2]) *
                                         record.parameters.dimensions[3]);
        }
        if (result_kind(record.destination->kind)) {
            entries[1].size = record.bytes;
            wgpuCommandEncoderClearBuffer(encoder, record.destination->buffer, 0, record.bytes);
        }
        ++count;
    }
    entries[count] = WGPU_BIND_GROUP_ENTRY_INIT;
    entries[count].binding = 2;
    entries[count].buffer = recording.uniform;
    entries[count].offset = slot * recording.stride;
    entries[count].size = sizeof(Parameters);
    ++count;
    if (record.coverage) {
        const auto axis = record.destination ? 2u : 0u;
        entries[count] = WGPU_BIND_GROUP_ENTRY_INIT;
        entries[count].binding = 3;
        entries[count].buffer = record.coverage->buffer;
        entries[count].size = mask_bytes(std::uint64_t(record.parameters.dimensions[axis]) *
                                         record.parameters.dimensions[axis + 1]);
        ++count;
    }
    for (std::size_t i = 0; i < record.sources.size(); ++i) {
        const auto& source = record.sources[i];
        if (!source) continue;
        entries[count] = WGPU_BIND_GROUP_ENTRY_INIT;
        entries[count].binding = static_cast<std::uint32_t>(4 + i);
        entries[count].buffer = source->buffer;
        entries[count].size = source->kind == ResourceKind::mask
                                  ? mask_bytes(source->capacity)
                                  : source->capacity * pixel_bytes;
        ++count;
    }
    if (record.data_offset != no_data) {
        entries[count] = WGPU_BIND_GROUP_ENTRY_INIT;
        entries[count].binding = 7;
        entries[count].buffer = recording.storage;
        entries[count].offset = record.data_offset * 4;
        entries[count].size = std::max<std::uint64_t>(record.data_count, 1) * 4;
        ++count;
    }
    WGPUBindGroupDescriptor bind_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bind_desc.layout = state.layouts[index];
    bind_desc.entryCount = count;
    bind_desc.entries = entries.data();
    Native<WGPUBindGroup, wgpuBindGroupRelease> group(
        wgpuDeviceCreateBindGroup(state.device, &bind_desc));
    if (!group.value) {
        fail(ErrorCode::execution_failed, "submit", {}, "bind group creation failed");
    }
    WGPUComputePassDescriptor pass_desc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    Native<WGPUComputePassEncoder, wgpuComputePassEncoderRelease> pass(
        wgpuCommandEncoderBeginComputePass(encoder, &pass_desc));
    if (!pass.value) {
        fail(ErrorCode::execution_failed, "submit", {}, "compute pass creation failed");
    }
    wgpuComputePassEncoderSetPipeline(pass.value, state.pipelines[index]);
    wgpuComputePassEncoderSetBindGroup(pass.value, 0, group.value, 0, nullptr);
    auto dispatch_width = record.parameters.dispatch[2];
    auto dispatch_height = record.parameters.dispatch[3];
    if (record.kernel == Kernel::mask_extract) {
        // These kernels index packed words using the original pixel-width stride.
        // Cover only rows containing words, including the final partial word.
        const auto words = (std::uint64_t(dispatch_width) * dispatch_height + 3) / 4;
        dispatch_height = static_cast<std::uint32_t>((words + dispatch_width - 1) / dispatch_width);
        dispatch_width = static_cast<std::uint32_t>(std::min<std::uint64_t>(dispatch_width, words));
    }
    if (record.workgroups[0]) {
        wgpuComputePassEncoderDispatchWorkgroups(pass.value, record.workgroups[0],
                                                 record.workgroups[1], record.workgroups[2]);
    } else {
        wgpuComputePassEncoderDispatchWorkgroups(pass.value, (dispatch_width + 7) / 8,
                                                 (dispatch_height + 7) / 8, 1);
    }
    wgpuComputePassEncoderEnd(pass.value);
    if (record.destination && record.destination->readback) {
        wgpuCommandEncoderCopyBufferToBuffer(encoder, record.destination->buffer, 0,
                                             record.destination->readback, 0,
                                             mask_bytes(record.bytes));
    }
}
} // namespace

ErrorScope::ErrorScope(State& state) : state_(state) {
    wgpuDevicePushErrorScope(state.device, WGPUErrorFilter_OutOfMemory);
    wgpuDevicePushErrorScope(state.device, WGPUErrorFilter_Validation);
}
ErrorScope::~ErrorScope() {
    if (active_) {
        ScopeResult ignored;
        pop_scope(state_, ignored);
        pop_scope(state_, ignored);
    }
}
void ErrorScope::finish(std::string_view operation) {
    ScopeResult result;
    pop_scope(state_, result);
    pop_scope(state_, result);
    active_ = false;
    if (result.code != static_cast<ErrorCode>(0)) {
        fail(result.code, operation, {},
             result.message[0] ? result.message : "WebGPU operation failed");
    }
    state_.check(operation);
}

namespace {
std::uint64_t& category_bytes(MemoryUsage& usage, MemoryCategory category) {
    switch (category) {
    case MemoryCategory::images: return usage.images;
    case MemoryCategory::masks: return usage.masks;
    case MemoryCategory::transfers: return usage.transfers;
    case MemoryCategory::internal: return usage.internal;
    case MemoryCategory::presentation: return usage.presentation;
    case MemoryCategory::workspace: return usage.workspace;
    }
    std::unreachable();
}
}

void MemoryLedger::reserve(std::uint64_t bytes, MemoryCategory,
                            std::string_view operation) {
    std::lock_guard lock(mutex);
    const auto ceiling = limit ? limit : std::numeric_limits<std::uint64_t>::max();
    const auto occupied = usage.total + reserved;
    if (occupied > ceiling || bytes > ceiling - occupied) {
        fail(ErrorCode::capacity, operation, "memory_limit", "GPU memory limit exceeded");
    }
    reserved += bytes;
}
void MemoryLedger::release(std::uint64_t bytes, MemoryCategory category, bool committed) noexcept {
    std::lock_guard lock(mutex);
    if (committed) {
        category_bytes(usage, category) -= bytes;
        usage.total -= bytes;
    } else {
        reserved -= bytes;
    }
}
void MemoryLedger::commit(std::uint64_t bytes, MemoryCategory category) noexcept {
    std::lock_guard lock(mutex);
    reserved -= bytes;
    category_bytes(usage, category) += bytes;
    usage.total += bytes;
    usage.peak = std::max(usage.peak, usage.total);
}

GpuBuffer allocate_buffer(State& state, std::uint64_t bytes, WGPUBufferUsage usage,
                           std::string_view operation, MemoryCategory category) {
    GpuBuffer buffer(std::make_shared<GpuBuffer::Owner>(state.ledger, bytes, category, operation));
    ErrorScope scope(state);
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.label = sv(operation);
    descriptor.size = bytes;
    descriptor.usage = usage;
    buffer.owner_->handle = wgpuDeviceCreateBuffer(state.device, &descriptor);
    scope.finish(operation);
    if (!buffer) {
        fail(ErrorCode::out_of_memory, operation, {}, "buffer allocation failed");
    }
    buffer.owner_->commit();
    return buffer;
}

GpuTexture allocate_texture(State& state, std::uint32_t width, std::uint32_t height,
                             std::string_view operation) {
    GpuTexture texture(std::make_shared<GpuTexture::Owner>(
        state.ledger, std::uint64_t(width) * height * 4, MemoryCategory::presentation, operation));
    ErrorScope scope(state);
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.label = sv(operation);
    descriptor.size = {width, height, 1};
    descriptor.format = WGPUTextureFormat_RGBA8Unorm;
    descriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    texture.owner_->handle = wgpuDeviceCreateTexture(state.device, &descriptor);
    scope.finish(operation);
    if (!texture) {
        fail(ErrorCode::out_of_memory, operation, {}, "texture allocation failed");
    }
    texture.owner_->commit();
    return texture;
}

std::shared_ptr<State> require_state(const std::shared_ptr<State>& state,
                                     std::string_view operation) {
    if (!state) {
        fail(ErrorCode::invalid_resource, operation, "context", "context is not initialized");
    }
    return state;
}
void State::report_error(ErrorCode code, WGPUStringView message) noexcept {
    // Publish one immutable diagnostic; callbacks never allocate or acquire the context mutex.
    if (!diagnostic_claimed.test_and_set()) {
        if (message.data) {
            const auto length = message.length == WGPU_STRLEN ? std::strlen(message.data)
                                                              : message.length;
            std::memcpy(diagnostic, message.data, std::min(length, sizeof(diagnostic) - 1));
        }
        diagnostic_code.store(code);
    }
    if (code == ErrorCode::device_lost) {
        asynchronous_error.store(code);
    } else {
        auto expected = static_cast<ErrorCode>(0);
        asynchronous_error.compare_exchange_strong(expected, code);
    }
}
void State::check(std::string_view operation) const {
    const auto error = asynchronous_error.load();
    if (error != static_cast<ErrorCode>(0)) {
        const auto message = diagnostic_code.load() == error && diagnostic[0] ? diagnostic
            : error == ErrorCode::device_lost ? "GPU device was lost" : "GPU execution failed";
        fail(error, operation, {}, message);
    }
}
Flight::~Flight() = default;

void State::retire(const std::shared_ptr<Flight>& flight) {
    if (flight->retired) {
        return;
    }
    const auto global_error = asynchronous_error.load();
    if (global_error != static_cast<ErrorCode>(0)) {
        flight->error.store(global_error);
        flight->asynchronous_failure = true;
    }
    const bool succeeded = flight->error.load() == static_cast<ErrorCode>(0);
    for (const auto& record : flight->records) {
        if (!succeeded && record.recording_guard) record.recording_guard->invalidate();
        for (const auto& source : record.sources) {
            if (source) --source->pending;
        }
        if (record.source) {
            --record.source->pending;
        }
        if (record.coverage) {
            --record.coverage->pending;
        }
        if (record.destination) {
            --record.destination->pending;
            if (record.destination->readback) {
                record.destination->valid_bytes = succeeded ? record.bytes : 0;
            }
        }
    }
    flight->records.clear();
    flight->uniform.reset();
    flight->storage.reset();
    flight->buffers.clear();
    flight->retired = true;
}

State::~State() {
    if (device) {
        drain(*this);
        wgpuDeviceDestroy(device);
    }
    for (auto& flight : flights) {
        retire(flight);
    }
    for (const auto& entry : resources) {
        if (auto resource = entry.lock()) {
            resource->alive = false;
            resource->release();
        }
    }
    resources.clear();
    for (auto layout : layouts) {
        if (layout) {
            wgpuBindGroupLayoutRelease(layout);
        }
    }
    for (auto pipeline : pipelines) {
        if (pipeline) {
            wgpuComputePipelineRelease(pipeline);
        }
    }
    if (queue) {
        wgpuQueueRelease(queue);
    }
    if (device) {
        wgpuDeviceRelease(device);
    }
    // Native callbacks retain Flight pointers until the device has drained them.
    flights.clear();
    if (adapter) {
        wgpuAdapterRelease(adapter);
    }
    if (instance) {
        wgpuInstanceRelease(instance);
    }
}
} // namespace wgpupixel::detail

namespace wgpupixel {
using namespace detail;
Context::Context(std::shared_ptr<State> state) : state_(std::move(state)) {}
Context::~Context() = default;
Context::Context(Context&&) noexcept = default;
Context& Context::operator=(Context&&) noexcept = default;
Submission::Submission(std::shared_ptr<Flight> flight) : flight_(std::move(flight)) {}

Context Context::create() try {
    auto state = std::make_shared<State>();
    state->instance = create_instance();
    if (!state->instance) {
        fail(ErrorCode::execution_failed, "create", {}, "WebGPU instance creation failed");
    }
    WGPURequestAdapterCallbackInfo adapter_info = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    adapter_info.mode = WGPUCallbackMode_AllowSpontaneous;
    adapter_info.userdata1 = state.get();
    adapter_info.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView,
                               void* userdata, void*) noexcept {
        auto& out = *static_cast<State*>(userdata);
        if (status == WGPURequestAdapterStatus_Success) {
            out.adapter = adapter;
        } else if (adapter) {
            wgpuAdapterRelease(adapter);
        }
    };
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.powerPreference = WGPUPowerPreference_HighPerformance;
    await_future(*state, wgpuInstanceRequestAdapter(state->instance, &options, adapter_info));
    if (!state->adapter) {
        fail(ErrorCode::execution_failed, "create", {}, "no compatible WebGPU adapter available");
    }
    WGPULimits supported = WGPU_LIMITS_INIT;
    if (wgpuAdapterGetLimits(state->adapter, &supported) != WGPUStatus_Success) {
        fail(ErrorCode::execution_failed, "create", {}, "adapter limits unavailable");
    }
    WGPULimits requested = WGPU_LIMITS_INIT;
    requested.maxBufferSize = supported.maxBufferSize;
    requested.maxStorageBufferBindingSize = supported.maxStorageBufferBindingSize;
    requested.maxComputeWorkgroupsPerDimension = supported.maxComputeWorkgroupsPerDimension;
    // Display and Presenter targets are 2D textures; the default 8192 is too small for
    // multi-monitor spans and large offscreen targets.
    requested.maxTextureDimension2D = supported.maxTextureDimension2D;
    WGPUDeviceDescriptor device_desc = WGPU_DEVICE_DESCRIPTOR_INIT;
    device_desc.requiredLimits = &requested;
    device_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    device_desc.deviceLostCallbackInfo.userdata1 = state.get();
    device_desc.deviceLostCallbackInfo.callback = [](WGPUDevice const*, WGPUDeviceLostReason reason,
                                                     WGPUStringView message, void* userdata,
                                                     void*) noexcept {
        if (reason != WGPUDeviceLostReason_Destroyed) {
            static_cast<State*>(userdata)->report_error(ErrorCode::device_lost, message);
        }
    };
    device_desc.uncapturedErrorCallbackInfo.userdata1 = state.get();
    device_desc.uncapturedErrorCallbackInfo.callback =
        [](WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void* userdata, void*) noexcept {
            static_cast<State*>(userdata)->report_error(type == WGPUErrorType_OutOfMemory
                                                        ? ErrorCode::out_of_memory
                                                        : ErrorCode::execution_failed, message);
        };
    WGPURequestDeviceCallbackInfo device_info = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    device_info.mode = WGPUCallbackMode_AllowSpontaneous;
    device_info.userdata1 = state.get();
    device_info.callback = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView,
                              void* userdata, void*) noexcept {
        if (status == WGPURequestDeviceStatus_Success) {
            static_cast<State*>(userdata)->device = device;
        } else if (device) {
            wgpuDeviceRelease(device);
        }
    };
    await_future(*state, wgpuAdapterRequestDevice(state->adapter, &device_desc, device_info));
    if (!state->device) {
        fail(ErrorCode::execution_failed, "create", {}, "GPU device creation failed");
    }
    if (wgpuDeviceGetLimits(state->device, &state->limits) != WGPUStatus_Success) {
        fail(ErrorCode::execution_failed, "create", {}, "device limits unavailable");
    }
    state->queue = wgpuDeviceGetQueue(state->device);
    if (!state->queue) {
        fail(ErrorCode::execution_failed, "create", {}, "GPU queue unavailable");
    }
    return Context(std::move(state));
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, "create", {}, "host allocation failed");
}

void Context::prepare() try {
    auto state = require_state(state_, "prepare");
    std::lock_guard lock(state->mutex);
    state->check("prepare");
#define WGPUPIXEL_KERNEL(name)                                                                     \
    make_pipeline(*state, Kernel::name, name##_shader());                                          \
    if (!name##_shader().masked_source.empty())                                                    \
        make_pipeline(*state, Kernel::name, name##_shader(), true);
#include "kernel_list.inc"
#undef WGPUPIXEL_KERNEL
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, "prepare", {}, "host allocation failed");
}

MemoryUsage Context::memory() const {
    auto state = require_state(state_, "memory");
    std::lock_guard state_lock(state->mutex);
    std::lock_guard lock(state->ledger->mutex);
    return state->ledger->usage;
}
void Context::set_memory_limit(std::uint64_t bytes) {
    auto state = require_state(state_, "set_memory_limit");
    std::lock_guard state_lock(state->mutex);
    std::lock_guard lock(state->ledger->mutex);
    state->ledger->limit = bytes;
}
void Context::reset_peak() {
    auto state = require_state(state_, "reset_peak");
    std::lock_guard state_lock(state->mutex);
    std::lock_guard lock(state->ledger->mutex);
    state->ledger->usage.peak = state->ledger->usage.total;
}

ResourceLimits Context::limits() const {
    auto state = require_state(state_, "limits");
    std::lock_guard lock(state->mutex);
    state->check("limits");
    const auto& limits = state->limits;
    const auto storage = std::min(limits.maxBufferSize, limits.maxStorageBufferBindingSize);
    return {limits.maxBufferSize, limits.maxStorageBufferBindingSize,
            std::min<std::uint64_t>(UINT32_MAX, storage / pixel_bytes),
            std::min<std::uint64_t>(UINT32_MAX, storage / 4 * 4),
            static_cast<std::uint32_t>(std::min<std::uint64_t>(INT32_MAX,
                8ull * limits.maxComputeWorkgroupsPerDimension)),
            storage / 4 * 4, limits.maxBufferSize / 4 * 4,
            limits.minStorageBufferOffsetAlignment, limits.maxTextureDimension2D};
}

Commands Context::create_commands(std::size_t capacity) try {
    auto state = require_state(state_, "create_commands");
    std::lock_guard lock(state->mutex);
    state->check("create_commands");
    capacity = std::max<std::size_t>(capacity, 1);
    const std::size_t alignment = state->limits.minUniformBufferOffsetAlignment;
    const std::size_t stride = ((sizeof(Parameters) + alignment - 1) / alignment) * alignment;
    if (capacity > std::numeric_limits<std::size_t>::max() / stride ||
        capacity > std::vector<std::byte>().max_size() / stride ||
        capacity > state->limits.maxBufferSize / stride ||
        capacity > std::vector<Record>().max_size()) {
        fail(ErrorCode::capacity, "create_commands", "command_capacity",
             "command capacity exceeds device or host limits");
    }
    auto recording = std::make_unique<Recording>();
    recording->owner = state;
    recording->capacity = capacity;
    recording->stride = stride;
    recording->records.reserve(capacity);
    recording->parameters.resize(capacity * stride);
    recording->uniform =
        allocate_buffer(*state, capacity * stride,
                        WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, "create_commands");
    return Commands(std::move(recording));
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, "create_commands", {}, "host allocation failed");
}

Submission Context::submit(Commands& commands) try {
    auto state = require_state(state_, "submit");
    std::lock_guard lock(state->mutex);
    state->check("submit");
    if (!commands.recording_) {
        fail(ErrorCode::invalid_resource, "submit", "commands", "commands are not initialized");
    }
    auto& recording = *commands.recording_;
    if (recording.owner != state) {
        fail(ErrorCode::invalid_resource, "submit", "commands",
             "commands belong to another context");
    }
    recording.require_ready("submit");
    for (const auto& record : recording.records) {
        if (record.recording_guard && record.recording_guard->valid &&
            !*record.recording_guard->valid) {
            fail(ErrorCode::invalid_resource, "submit", "state",
                 "stroke recording depends on discarded or failed work; reset the stroke state");
        }
    }
    // Compile the pipelines this recording uses on first use, before any GPU work.
    for (const auto& record : recording.records) {
        if (record.kind == RecordKind::kernel) {
            make_pipeline(*state, record.kernel, kernel_shader(record.kernel), bool(record.coverage));
        }
    }
    const auto data_bytes = std::uint64_t(recording.data.size()) * 4;
    if (data_bytes > recording.storage_bytes) {
        // Grow geometrically; the old buffer stays alive while a retired flight holds it.
        const auto bytes = std::max(data_bytes, std::min<std::uint64_t>(
                                                    2 * recording.storage_bytes,
                                                    state->limits.maxBufferSize / 4 * 4));
        auto storage = allocate_buffer(*state, bytes,
                                       WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, "submit");
        recording.storage = std::move(storage);
        recording.storage_bytes = bytes;
    }
    auto flight = std::make_shared<Flight>();
    flight->owner = state;
    state->flights.reserve(state->flights.size() + 1);
    recording.recycle();
    using CommandBuffer = Native<WGPUCommandBuffer, wgpuCommandBufferRelease>;
    std::vector<CommandBuffer> buffers;
    std::size_t completed_records = 0;
    bool submitted_internal = false;
    try {
        ErrorScope scope(*state);
        WGPUCommandEncoderDescriptor encoder_desc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
        Native<WGPUCommandEncoder, wgpuCommandEncoderRelease> encoder(
            wgpuDeviceCreateCommandEncoder(state->device, &encoder_desc));
        if (!encoder.value) {
            fail(ErrorCode::execution_failed, "submit", {}, "command encoder creation failed");
        }
        buffers.reserve(16);
        for (std::size_t i = 0; i < recording.records.size(); ++i) {
            std::memcpy(recording.parameters.data() + i * recording.stride,
                        &recording.records[i].parameters, sizeof(Parameters));
        }
        if (!recording.records.empty()) {
            wgpuQueueWriteBuffer(state->queue, recording.uniform, 0, recording.parameters.data(),
                                 recording.records.size() * recording.stride);
        }
        if (data_bytes) {
            wgpuQueueWriteBuffer(state->queue, recording.storage, 0, recording.data.data(), data_bytes);
        }
        std::size_t encoded = 0;
        const auto finish_batch = [&] {
            WGPUCommandBufferDescriptor descriptor = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
            buffers.emplace_back(wgpuCommandEncoderFinish(encoder.value, &descriptor));
            if (!buffers.back().value) {
                fail(ErrorCode::execution_failed, "submit", {}, "command buffer creation failed");
            }
            if (buffers.size() == 16) {
                // Bound native command storage too; recorded resources retain these early batches.
                auto completed = std::make_shared<Flight>();
                auto callback_owner = std::make_unique<std::shared_ptr<Flight>>(completed);
                submitted_internal = true;
                for (const auto& buffer : buffers) {
                    completed->index = submit_queue(*state, buffer.value);
                }
                WGPUQueueWorkDoneCallbackInfo info = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
                info.mode = WGPUCallbackMode_AllowSpontaneous;
                info.userdata1 = callback_owner.release();
                info.callback = [](WGPUQueueWorkDoneStatus status, WGPUStringView, void* userdata,
                                   void*) noexcept {
                    std::unique_ptr<std::shared_ptr<Flight>> owner(
                        static_cast<std::shared_ptr<Flight>*>(userdata));
                    if (status != WGPUQueueWorkDoneStatus_Success) {
                        (*owner)->error.store(ErrorCode::execution_failed);
                    }
                    (*owner)->done.store(true);
                };
                completed->completion = wgpuQueueOnSubmittedWorkDone(state->queue, info);
                for (; completed_records < encoded; ++completed_records) {
                    const auto& record = recording.records[completed_records];
                    record.output().mark_written();
                    if (record.destination && record.destination->readback) {
                        record.destination->valid_bytes = 0;
                    }
                }
                drain(*state, completed.get());
                state->check("submit");
                if (completed->error.load() != static_cast<ErrorCode>(0)) {
                    fail(ErrorCode::execution_failed, "submit", {}, "internal batch submission failed");
                }
                buffers.clear();
            }
        };
        for (std::size_t i = 0; i < recording.records.size(); ++i) {
            const auto& record = recording.records[i];
            if (record.starts_batch && i != 0) {
                finish_batch();
                wgpuCommandEncoderRelease(encoder.value);
                encoder.value = wgpuDeviceCreateCommandEncoder(state->device, &encoder_desc);
                if (!encoder.value) {
                    fail(ErrorCode::execution_failed, "submit", {}, "command encoder creation failed");
                }
            }
            encode_record(*state, encoder.value, recording, record, i);
            encoded = i + 1;
        }
        finish_batch();
        scope.finish("submit");
    } catch (const Error& error) {
        if (!submitted_internal) throw;
        // An internal batch may already have changed the destination. Replaying
        // the original recording would apply that prefix twice.
        recording.clear();
        char message[1024];
        std::snprintf(message, sizeof(message),
                      "internal batches may have executed; recording discarded and output is not valid: %s",
                      error.what());
        throw Error(error.code(), "submit", error.parameter(), message);
    } catch (const std::bad_alloc&) {
        if (!submitted_internal) throw;
        recording.clear();
        fail(ErrorCode::out_of_memory, "submit", {},
             "host allocation failed after internal submission; recording discarded and output is not valid");
    } catch (...) {
        if (submitted_internal) recording.clear();
        throw;
    }

    ErrorScope submission_scope(*state);
    if (data_bytes) {
        flight->storage = recording.storage.retain();
    }
    recording.data.clear();
    flight->records.swap(recording.records);
    // Both owners retain the buffer so Commands can also be destroyed while pending.
    flight->uniform = recording.uniform.retain();
    for (const auto& record : flight->records) {
        for (const auto& source : record.sources) {
            if (source) {
                --source->recorded;
                ++source->pending;
            }
        }
        if (record.source) {
            --record.source->recorded;
            ++record.source->pending;
        }
        if (record.coverage) {
            --record.coverage->recorded;
            ++record.coverage->pending;
        }
        if (record.destination) {
            --record.destination->recorded;
            ++record.destination->pending;
            if (record.destination->readback) {
                record.destination->valid_bytes = 0;
            }
        }
    }
    recording.flight = flight;
    state->flights.push_back(flight);
    // One completion covers the ordered queue, including bounded internal batches.
    for (const auto& buffer : buffers) {
        flight->index = submit_queue(*state, buffer.value);
    }
    for (std::size_t i = completed_records; i < flight->records.size(); ++i) {
        flight->records[i].output().mark_written();
    }
    WGPUQueueWorkDoneCallbackInfo info = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
    info.mode = WGPUCallbackMode_AllowSpontaneous;
    info.userdata1 = flight.get();
    info.callback = [](WGPUQueueWorkDoneStatus status, WGPUStringView, void* userdata,
                       void*) noexcept {
        auto& out = *static_cast<Flight*>(userdata);
        if (status != WGPUQueueWorkDoneStatus_Success) {
            out.error.store(ErrorCode::execution_failed);
        }
        out.done.store(true);
    };
    flight->completion = wgpuQueueOnSubmittedWorkDone(state->queue, info);
    try {
        submission_scope.finish("submit");
    } catch (const Error& error) {
        flight->error.store(error.code());
    }
    return Submission(std::move(flight));
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, "submit", {}, "host allocation failed");
}

bool Context::is_complete(const Submission& submission) {
    auto state = require_state(state_, "is_complete");
    std::lock_guard lock(state->mutex);
    if (!submission.flight_ || submission.flight_->owner.lock() != state) {
        fail(ErrorCode::invalid_resource, "is_complete", "submission",
             "submission belongs to no live context or another context");
    }
#ifndef __EMSCRIPTEN__
    if (state->asynchronous_error.load() != ErrorCode::device_lost)
        wgpuDevicePoll(state->device, false, nullptr);
#endif
    const bool lost = state->asynchronous_error.load() == ErrorCode::device_lost;
    for (const auto& pending : state->flights) {
        if (pending->done.load() || lost) state->retire(pending);
    }
    std::erase_if(state->flights,
                  [](const auto& pending) { return pending->retired && pending->done.load(); });
    const auto& flight = submission.flight_;
    if (!flight->retired) return false;
    const auto error = flight->error.load();
    if (error != static_cast<ErrorCode>(0)) {
        if (flight->asynchronous_failure && state->diagnostic_code.load() == error &&
            state->diagnostic[0]) {
            fail(error, "is_complete", {}, state->diagnostic);
        }
        fail(error, "is_complete", {},
             error == ErrorCode::device_lost ? "GPU device was lost"
                                             : "submission failed; output is not valid");
    }
    return true;
}

void Context::wait(const Submission& submission) {
    auto state = require_state(state_, "wait");
    std::lock_guard lock(state->mutex);
    if (!submission.flight_ || submission.flight_->owner.lock() != state) {
        fail(ErrorCode::invalid_resource, "wait", "submission",
             "submission belongs to no live context or another context");
    }
    auto& flight = submission.flight_;
    if (!flight->retired) {
        drain(*state, flight.get());
        const bool lost = state->asynchronous_error.load() == ErrorCode::device_lost;
        for (const auto& pending : state->flights) {
            if (pending->done.load() || lost) {
                state->retire(pending);
            }
        }
        std::erase_if(state->flights,
                      [](const auto& pending) { return pending->retired && pending->done.load(); });
        if (!flight->retired) {
            state->check("wait");
            fail(ErrorCode::execution_failed, "wait", {}, "GPU completion was not reported");
        }
    }
    const auto error = flight->error.load();
    if (error != static_cast<ErrorCode>(0)) {
        if (flight->asynchronous_failure && state->diagnostic_code.load() == error &&
            state->diagnostic[0]) {
            fail(error, "wait", {}, state->diagnostic);
        }
        fail(error, "wait", {},
             error == ErrorCode::device_lost ? "GPU device was lost"
                                             : "submission failed; output is not valid");
    }
}

// upload.wgsl and download.wgsl in float32: same constants, branches and operation order.
Color from_srgb(float r, float g, float b, float a) noexcept {
    const float alpha = std::clamp(a, 0.0f, 1.0f);
    if (alpha == 0) {
        return {0, 0, 0, 0};
    }
    const auto linear = [alpha](float encoded) {
        const float v = std::clamp(encoded, 0.0f, 1.0f);
        return (v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f)) * alpha;
    };
    return {linear(r), linear(g), linear(b), alpha};
}

std::array<float, 4> to_srgb(Color color) noexcept {
    if (color.a <= 0) {
        return {0, 0, 0, 0};
    }
    const auto encoded = [&](float channel) {
        const float v = std::clamp(channel / color.a, 0.0f, 1.0f);
        return v <= 0.0031308f ? 12.92f * v
                               : 1.055f * std::pow(v, static_cast<float>(1.0 / 2.4)) - 0.055f;
    };
    return {encoded(color.r), encoded(color.g), encoded(color.b), std::min(color.a, 1.0f)};
}
} // namespace wgpupixel
