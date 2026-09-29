#include "runtime.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace wgpupixel::detail {

void State::register_resource(const std::shared_ptr<Resource>& resource) {
    // Reclaim expired entries before growing, without rescanning every live handle
    // on each allocation. The registry does not extend resource lifetime.
    if (resources.size() == resources.capacity()) {
        std::erase_if(resources, [](const auto& entry) { return entry.expired(); });
    }
    resources.push_back(resource);
}

Resource::~Resource() {
    release();
}

void Resource::release() noexcept {
    buffer.reset();
    readback.reset();
}

std::shared_ptr<State> resource_owner(const std::shared_ptr<Resource>& resource,
                                      std::string_view operation) {
    if (!resource) {
        fail(ErrorCode::invalid_resource, operation, "resource", "resource is empty");
    }
    auto owner = resource->owner.lock();
    if (!owner) {
        fail(ErrorCode::invalid_resource, operation, "resource",
             "resource context no longer exists");
    }
    return owner;
}

void validate_resource(const std::shared_ptr<State>& state,
                       const std::shared_ptr<Resource>& resource, ResourceKind kind,
                       std::string_view operation, std::string_view parameter) {
    if (!resource || resource->owner.lock() != state) {
        fail(ErrorCode::invalid_resource, operation, parameter,
             "resource is empty or belongs to another context");
    }
    if (!resource->alive || !resource->buffer || resource->kind != kind) {
        fail(ErrorCode::invalid_resource, operation, parameter,
             "resource has been destroyed or has an incompatible kind");
    }
}

std::uint64_t validate_size(const State& state, std::int64_t width, std::int64_t height,
                            std::string_view operation, std::uint64_t bytes_per_pixel) {
    constexpr auto maximum = std::numeric_limits<std::int32_t>::max();
    if (width <= 0 || width > maximum) {
        fail(ErrorCode::invalid_argument, operation, "size.width",
             "width must be positive and fit int32");
    }
    if (height <= 0 || height > maximum) {
        fail(ErrorCode::invalid_argument, operation, "size.height",
             "height must be positive and fit int32");
    }
    const auto w = static_cast<std::uint64_t>(width);
    const auto h = static_cast<std::uint64_t>(height);
    const auto pixels = w * h;
    if (pixels > std::numeric_limits<std::uint32_t>::max()) {
        fail(ErrorCode::capacity, operation, "size",
             "pixel count exceeds shader address capacity");
    }
    const auto bytes = mask_bytes(pixels * bytes_per_pixel);
    if (bytes > state.limits.maxBufferSize || bytes > state.limits.maxStorageBufferBindingSize) {
        fail(ErrorCode::capacity, operation, "size", "image exceeds device buffer capacity");
    }
    if ((w + 7) / 8 > state.limits.maxComputeWorkgroupsPerDimension ||
        (h + 7) / 8 > state.limits.maxComputeWorkgroupsPerDimension) {
        fail(ErrorCode::capacity, operation, "size",
             "image exceeds device dispatch dimensions");
    }
    return pixels;
}

} // namespace wgpupixel::detail

namespace wgpupixel {
namespace {

using detail::Resource;
using detail::ResourceKind;
using detail::State;

std::uint64_t capacity(const std::shared_ptr<Resource>& resource, ResourceKind kind,
                       std::string_view operation) {
    auto state = detail::resource_owner(resource, operation);
    std::lock_guard lock(state->mutex);
    state->check(operation);
    detail::validate_resource(state, resource, kind, operation, "resource");
    return resource->capacity;
}

std::uint64_t resource_revision(const std::shared_ptr<Resource>& resource, ResourceKind kind) {
    constexpr std::string_view operation = "revision";
    auto state = detail::resource_owner(resource, operation);
    std::lock_guard lock(state->mutex);
    state->check(operation);
    detail::validate_resource(state, resource, kind, operation, "resource");
    return resource->revision();
}

std::shared_ptr<Resource> create_transfer(const std::shared_ptr<State>& state,
                                          const std::shared_ptr<Resource>& image, ResourceKind kind,
                                          std::string_view operation,
                                          ResourceKind source_kind = ResourceKind::image,
                                          const TransferBufferOptions& options = {}) {
    detail::validate_resource(state, image, source_kind, operation,
                              source_kind == ResourceKind::mask ? "mask" : "image");
    if (std::to_underlying(options.format) > std::to_underlying(TransferFormat::rgba32_float)) {
        detail::fail(ErrorCode::invalid_argument, operation, "format", "transfer format is invalid");
    }
    auto resource = std::make_shared<Resource>();
    resource->owner = state;
    resource->kind = kind;
    resource->capacity = options.capacity_pixels ? options.capacity_pixels : image->capacity;
    resource->format = options.format;
    resource->element_bytes =
        source_kind == ResourceKind::mask ? 1 : detail::transfer_bytes(options.format);
    const auto& limits = state->limits;
    if (resource->capacity > std::numeric_limits<std::uint32_t>::max() ||
        detail::mask_bytes(resource->capacity * resource->element_bytes) >
            std::min(limits.maxBufferSize, limits.maxStorageBufferBindingSize)) {
        detail::fail(ErrorCode::capacity, operation, "capacity_pixels",
                     "transfer buffer exceeds device buffer capacity");
    }
    const auto bytes = detail::mask_bytes(resource->capacity * resource->element_bytes);
    const auto usage =
        (source_kind == ResourceKind::mask ? WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst
                                           : WGPUBufferUsage_None) |
        WGPUBufferUsage_Storage |
        (kind == ResourceKind::upload ? WGPUBufferUsage_CopyDst : WGPUBufferUsage_CopySrc);
    resource->buffer = detail::allocate_buffer(
        *state, bytes, usage, operation, detail::MemoryCategory::transfers);
    if (kind == ResourceKind::readback) {
        resource->readback = detail::allocate_buffer(
            *state, bytes, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst, operation,
            detail::MemoryCategory::transfers);
    }
    state->register_resource(resource);
    return resource;
}

void validate_cpu_access(const Resource& resource, const std::uint8_t* data, std::size_t size,
                         std::string_view operation) {
    if (resource.recorded || resource.pending) {
        detail::fail(ErrorCode::resource_busy, operation, "buffer",
                     "buffer is recorded or pending on the GPU");
    }
    if (!data || !size || size % resource.element_bytes != 0) {
        detail::fail(ErrorCode::invalid_argument, operation, "pixels",
                     "pixels must be a nonempty span of complete resource elements");
    }
    if (size / resource.element_bytes > resource.capacity) {
        detail::fail(ErrorCode::capacity, operation, "pixels",
                     "pixel span exceeds buffer capacity");
    }
}

struct Mapping {
    WGPUBuffer buffer;
    bool active = false;
    ~Mapping() {
        if (active) {
            wgpuBufferUnmap(buffer);
        }
    }
};

void map_buffer(State& state, Mapping& mapping, WGPUMapMode mode, std::size_t bytes,
                std::string_view operation) {
    struct Completion {
        std::atomic<bool> done{false};
        WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Error;
    };
    auto completion = std::make_shared<Completion>();
    auto callback_owner = std::make_unique<std::shared_ptr<Completion>>(completion);
    WGPUBufferMapCallbackInfo callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowProcessEvents;
    callback.userdata1 = callback_owner.get();
    callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* userdata,
                           void*) noexcept {
        std::unique_ptr<std::shared_ptr<Completion>> owner(
            static_cast<std::shared_ptr<Completion>*>(userdata));
        auto& result = **owner;
        result.status = status;
        result.done.store(true, std::memory_order_release);
    };
    detail::ErrorScope errors(state);
    // Unmap also cancels a pending request if polling or device validation fails.
    // Its completion payload remains alive until the callback is delivered.
    mapping.active = true;
    callback_owner.release();
    const auto future = wgpuBufferMapAsync(mapping.buffer, mode, 0, bytes, callback);
    await_future(state, future);
    errors.finish(operation);
    while (!completion->done.load(std::memory_order_acquire)) {
        detail::ErrorScope polling_errors(state);
        drain(state);
        polling_errors.finish(operation);
    }
    mapping.active = completion->status == WGPUMapAsyncStatus_Success;
    state.check(operation);
    if (!mapping.active) {
        detail::fail(ErrorCode::execution_failed, operation, "buffer", "GPU buffer mapping failed");
    }
}

void unmap_buffer(State& state, Mapping& mapping, std::string_view operation) {
    detail::ErrorScope errors(state);
    mapping.active = false;
    wgpuBufferUnmap(mapping.buffer);
    errors.finish(operation);
}

void destroy_resource(const std::shared_ptr<State>& state,
                      const std::shared_ptr<Resource>& resource, ResourceKind kind) {
    constexpr std::string_view operation = "destroy";
    detail::validate_resource(state, resource, kind, operation, "resource");
    if (resource->recorded || resource->pending) {
        detail::fail(ErrorCode::resource_busy, operation, "resource",
                     "resource is recorded or pending on the GPU");
    }
    resource->release();
    resource->alive = false;
    resource->valid_bytes = 0;
    std::erase_if(state->resources, [&](const auto& entry) {
        return entry.expired() || entry.lock() == resource;
    });
}

} // namespace

void detail::read_mapped(State& state, const Resource& resource, std::span<std::byte> bytes,
                         std::string_view operation) {
    Mapping mapping{resource.readback};
    const auto mapped_bytes = static_cast<std::size_t>(detail::mask_bytes(bytes.size()));
    map_buffer(state, mapping, WGPUMapMode_Read, mapped_bytes, operation);
    detail::ErrorScope range_errors(state);
    const auto* source = wgpuBufferGetConstMappedRange(mapping.buffer, 0, mapped_bytes);
    range_errors.finish(operation);
    if (!source) {
        detail::fail(ErrorCode::execution_failed, operation, "buffer",
                     "mapped readback memory is unavailable");
    }
    std::memcpy(bytes.data(), source, bytes.size());
    unmap_buffer(state, mapping, operation);
}

Image::Image(std::shared_ptr<Resource> resource) : resource_(std::move(resource)) {}
Mask::Mask(std::shared_ptr<Resource> resource) : resource_(std::move(resource)) {}
UploadBuffer::UploadBuffer(std::shared_ptr<Resource> resource) : resource_(std::move(resource)) {}
ReadbackBuffer::ReadbackBuffer(std::shared_ptr<Resource> resource)
    : resource_(std::move(resource)) {}

ImageSize Image::size() const {
    auto state = detail::resource_owner(resource_, "size");
    std::lock_guard lock(state->mutex);
    state->check("size");
    detail::validate_resource(state, resource_, ResourceKind::image, "size", "image");
    return {resource_->width, resource_->height};
}

std::uint64_t Image::capacity_pixels() const {
    return capacity(resource_, ResourceKind::image, "capacity_pixels");
}

std::uint64_t Image::revision() const {
    return resource_revision(resource_, ResourceKind::image);
}

std::uint64_t Mask::revision() const {
    return resource_revision(resource_, ResourceKind::mask);
}
ImageSize Mask::size() const {
    auto state = detail::resource_owner(resource_, "size");
    std::lock_guard lock(state->mutex);
    state->check("size");
    detail::validate_resource(state, resource_, ResourceKind::mask, "size", "mask");
    return {resource_->width, resource_->height};
}

std::uint64_t Mask::capacity_pixels() const {
    return capacity(resource_, ResourceKind::mask, "capacity_pixels");
}
std::uint64_t UploadBuffer::capacity_pixels() const {
    return capacity(resource_, ResourceKind::upload, "capacity_pixels");
}
std::uint64_t ReadbackBuffer::capacity_pixels() const {
    return capacity(resource_, ResourceKind::readback, "capacity_pixels");
}

std::uint32_t UploadBuffer::bytes_per_pixel() const {
    constexpr std::string_view operation = "bytes_per_pixel";
    auto state = detail::resource_owner(resource_, operation);
    std::lock_guard lock(state->mutex);
    state->check(operation);
    detail::validate_resource(state, resource_, ResourceKind::upload, operation, "buffer");
    return resource_->element_bytes;
}

std::uint32_t ReadbackBuffer::bytes_per_pixel() const {
    constexpr std::string_view operation = "bytes_per_pixel";
    auto state = detail::resource_owner(resource_, operation);
    std::lock_guard lock(state->mutex);
    state->check(operation);
    detail::validate_resource(state, resource_, ResourceKind::readback, operation, "buffer");
    return resource_->element_bytes;
}

void Image::set_size(ImageSize size) {
    const auto [width, height] = size;
    constexpr std::string_view operation = "set_size";
    auto state = detail::resource_owner(resource_, operation);
    std::lock_guard lock(state->mutex);
    state->check(operation);
    detail::validate_resource(state, resource_, ResourceKind::image, operation, "image");
    const auto pixels = detail::validate_size(*state, width, height, operation);
    if (pixels > resource_->capacity) {
        detail::fail(ErrorCode::capacity, operation, "size",
                     "logical dimensions exceed reserved pixel capacity");
    }
    resource_->set_size(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
}

void Mask::set_size(ImageSize size) {
    const auto [width, height] = size;
    constexpr std::string_view operation = "set_size";
    auto state = detail::resource_owner(resource_, operation);
    std::lock_guard lock(state->mutex);
    state->check(operation);
    detail::validate_resource(state, resource_, ResourceKind::mask, operation, "mask");
    const auto pixels = detail::validate_size(*state, width, height, operation, 1);
    if (pixels > resource_->capacity) {
        detail::fail(ErrorCode::capacity, operation, "size",
                     "logical dimensions exceed reserved pixel capacity");
    }
    resource_->set_size(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
}

Image Context::create_image(ImageSize size) try {
    const auto [width, height] = size;
    constexpr std::string_view operation = "create_image";
    auto state = detail::require_state(state_, operation);
    std::lock_guard lock(state->mutex);
    state->check(operation);
    const auto pixels = detail::validate_size(*state, width, height, operation);
    auto resource = std::make_shared<Resource>();
    resource->owner = state;
    resource->kind = ResourceKind::image;
    resource->capacity = pixels;
    resource->width = static_cast<std::uint32_t>(width);
    resource->height = static_cast<std::uint32_t>(height);
    resource->buffer = detail::allocate_buffer(
        *state, pixels * detail::pixel_bytes,
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst, operation,
        detail::MemoryCategory::images);
    state->register_resource(resource);
    return Image(std::move(resource));
} catch (const std::bad_alloc&) {
    detail::fail(ErrorCode::out_of_memory, "create_image", "",
                 "could not allocate image resources");
}

Mask Context::create_mask(ImageSize size) try {
    const auto [width, height] = size;
    constexpr std::string_view operation = "create_mask";
    auto state = detail::require_state(state_, operation);
    std::lock_guard lock(state->mutex);
    state->check(operation);
    const auto pixels = detail::validate_size(*state, width, height, operation, 1);
    auto resource = std::make_shared<Resource>();
    resource->owner = state;
    resource->kind = ResourceKind::mask;
    resource->element_bytes = 1;
    resource->capacity = pixels;
    resource->width = static_cast<std::uint32_t>(width);
    resource->height = static_cast<std::uint32_t>(height);
    resource->buffer = detail::allocate_buffer(
        *state, detail::mask_bytes(pixels),
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst, operation,
        detail::MemoryCategory::masks);
    state->register_resource(resource);
    return Mask(std::move(resource));
} catch (const std::bad_alloc&) {
    detail::fail(ErrorCode::out_of_memory, "create_mask", "", "could not allocate mask resources");
}

UploadBuffer Context::create_upload_buffer(const Image& image,
                                           const TransferBufferOptions& options) try {
    constexpr std::string_view operation = "create_upload_buffer";
    auto state = detail::require_state(state_, operation);
    std::lock_guard lock(state->mutex);
    state->check(operation);
    return UploadBuffer(create_transfer(state, image.resource_, ResourceKind::upload, operation,
                                        ResourceKind::image, options));
} catch (const std::bad_alloc&) {
    detail::fail(ErrorCode::out_of_memory, "create_upload_buffer", "",
                 "could not allocate upload resources");
}

ReadbackBuffer Context::create_readback_buffer(const Image& image,
                                               const TransferBufferOptions& options) try {
    constexpr std::string_view operation = "create_readback_buffer";
    auto state = detail::require_state(state_, operation);
    std::lock_guard lock(state->mutex);
    state->check(operation);
    return ReadbackBuffer(create_transfer(state, image.resource_, ResourceKind::readback,
                                          operation, ResourceKind::image, options));
} catch (const std::bad_alloc&) {
    detail::fail(ErrorCode::out_of_memory, "create_readback_buffer", "",
                 "could not allocate readback resources");
}

UploadBuffer Context::create_upload_buffer(const Mask& mask,
                                           const MaskTransferBufferOptions& options) try {
    constexpr std::string_view operation = "create_upload_buffer";
    auto state = detail::require_state(state_, operation);
    std::lock_guard lock(state->mutex);
    state->check(operation);
    return UploadBuffer(create_transfer(state, mask.resource_, ResourceKind::upload, operation,
                                        ResourceKind::mask,
                                        {.capacity_pixels = options.capacity_pixels}));
} catch (const std::bad_alloc&) {
    detail::fail(ErrorCode::out_of_memory, "create_upload_buffer", "",
                 "could not allocate upload resources");
}

ReadbackBuffer Context::create_readback_buffer(const Mask& mask,
                                               const MaskTransferBufferOptions& options) try {
    constexpr std::string_view operation = "create_readback_buffer";
    auto state = detail::require_state(state_, operation);
    std::lock_guard lock(state->mutex);
    state->check(operation);
    return ReadbackBuffer(create_transfer(state, mask.resource_, ResourceKind::readback, operation,
                                          ResourceKind::mask,
                                          {.capacity_pixels = options.capacity_pixels}));
} catch (const std::bad_alloc&) {
    detail::fail(ErrorCode::out_of_memory, "create_readback_buffer", "",
                 "could not allocate readback resources");
}

void Context::write(const UploadBuffer& buffer, std::span<const std::uint8_t> pixels) try {
    constexpr std::string_view operation = "write";
    auto state = detail::require_state(state_, operation);
    std::lock_guard lock(state->mutex);
    state->check(operation);
    detail::validate_resource(state, buffer.resource_, ResourceKind::upload, operation, "buffer");
    validate_cpu_access(*buffer.resource_, pixels.data(), pixels.size(), operation);
    const auto bytes = pixels.size_bytes();
    buffer.resource_->valid_bytes = 0;
    detail::ErrorScope errors(*state);
    const auto aligned_bytes = bytes - bytes % 4;
    if (aligned_bytes) {
        wgpuQueueWriteBuffer(state->queue, buffer.resource_->buffer, 0, pixels.data(),
                             aligned_bytes);
    }
    if (aligned_bytes != bytes) {
        std::array<std::uint8_t, 4> tail{};
        std::memcpy(tail.data(), pixels.data() + aligned_bytes, bytes - aligned_bytes);
        wgpuQueueWriteBuffer(state->queue, buffer.resource_->buffer, aligned_bytes, tail.data(),
                             tail.size());
    }
    errors.finish(operation);
    buffer.resource_->valid_bytes = bytes;
} catch (const std::bad_alloc&) {
    detail::fail(ErrorCode::out_of_memory, "write", "", "could not prepare upload");
}

void Context::read(const ReadbackBuffer& buffer, std::span<std::uint8_t> pixels) try {
    constexpr std::string_view operation = "read";
    auto state = detail::require_state(state_, operation);
    std::lock_guard lock(state->mutex);
    state->check(operation);
    detail::validate_resource(state, buffer.resource_, ResourceKind::readback, operation, "buffer");
    validate_cpu_access(*buffer.resource_, pixels.data(), pixels.size(), operation);
    const auto bytes = pixels.size_bytes();
    if (bytes != buffer.resource_->valid_bytes) {
        detail::fail(ErrorCode::invalid_argument, operation, "pixels",
                     "span must match the last completed download size");
    }
    const auto mapped_bytes = detail::mask_bytes(bytes);
    if (mapped_bytes > std::numeric_limits<std::size_t>::max()) {
        detail::fail(ErrorCode::capacity, operation, "pixels",
                     "aligned mapping exceeds address capacity");
    }
    Mapping mapping{buffer.resource_->readback};
    map_buffer(*state, mapping, WGPUMapMode_Read, static_cast<std::size_t>(mapped_bytes),
               operation);
    detail::ErrorScope range_errors(*state);
    const auto* source =
        wgpuBufferGetConstMappedRange(mapping.buffer, 0, static_cast<std::size_t>(mapped_bytes));
    range_errors.finish(operation);
    if (!source) {
        detail::fail(ErrorCode::execution_failed, operation, "buffer",
                     "mapped readback memory is unavailable");
    }
    std::memcpy(pixels.data(), source, bytes);
    unmap_buffer(*state, mapping, operation);
} catch (const std::bad_alloc&) {
    detail::fail(ErrorCode::out_of_memory, "read", "", "could not prepare readback mapping");
}

void Context::destroy(const Image& image) {
    auto state = detail::require_state(state_, "destroy");
    std::lock_guard lock(state->mutex);
    destroy_resource(state, image.resource_, ResourceKind::image);
}

void Context::destroy(const Mask& mask) {
    auto state = detail::require_state(state_, "destroy");
    std::lock_guard lock(state->mutex);
    destroy_resource(state, mask.resource_, ResourceKind::mask);
}

void Context::destroy(const UploadBuffer& buffer) {
    auto state = detail::require_state(state_, "destroy");
    std::lock_guard lock(state->mutex);
    destroy_resource(state, buffer.resource_, ResourceKind::upload);
}

void Context::destroy(const ReadbackBuffer& buffer) {
    auto state = detail::require_state(state_, "destroy");
    std::lock_guard lock(state->mutex);
    destroy_resource(state, buffer.resource_, ResourceKind::readback);
}

} // namespace wgpupixel
