#include "wgpupixel_webgpu.h"
#include "runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <new>
#include <tuple>
#include <utility>
#include <vector>

namespace wgpupixel::detail {
// Area-averaged levels of one image or mask, cached by resource revision.
struct Pyramid {
    GpuBuffer buffer;
    std::uint64_t bytes = 0;
    std::weak_ptr<Resource> source;
    std::shared_ptr<Flight> generation;
    std::uint64_t revision = 0;
    std::uint32_t width = 0, height = 0;
    std::array<std::uint32_t, 3> key{}; // Axis levels: source level, factor, axis.
    bool current(const std::shared_ptr<Resource>& resource,
                 std::array<std::uint32_t, 3> wanted = {}) const {
        return buffer && generation && generation->error.load() == static_cast<ErrorCode>(0) &&
               source.lock() == resource && revision == resource->revision() &&
               width == resource->width && height == resource->height && key == wanted;
    }
};

// Fixed viewport resources are prepared once; large caches require explicit reserve.
struct Viewport {
    WGPURenderPipeline pipeline = nullptr;
    WGPUComputePipeline reduce = nullptr, axis = nullptr;
    WGPUBindGroupLayout layout = nullptr, reduce_layout = nullptr, axis_layout = nullptr;
    GpuBuffer uniform, reduce_uniform, empty;
    std::uint64_t reduce_stride = 0;
    Pyramid image, mask, image_axis, mask_axis;
    ~Viewport() {
        for (auto group : {layout, reduce_layout, axis_layout}) {
            if (group) wgpuBindGroupLayoutRelease(group);
        }
        for (auto compute : {reduce, axis}) {
            if (compute) wgpuComputePipelineRelease(compute);
        }
        if (pipeline) wgpuRenderPipelineRelease(pipeline);
    }
};

struct Presentation {
    std::shared_ptr<State> owner;
    WGPURenderPipeline pipeline = nullptr;
    WGPUBindGroupLayout layout = nullptr;
    GpuBuffer uniform;
    bool srgb = false;
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    std::unique_ptr<Viewport> viewport;

    static std::shared_ptr<State> context(const Context& context) {
        return require_state(context.state_, "native_context");
    }
    ~Presentation() {
        if (layout) wgpuBindGroupLayoutRelease(layout);
        if (pipeline) wgpuRenderPipelineRelease(pipeline);
    }
};
} // namespace wgpupixel::detail

namespace wgpupixel::webgpu {
using namespace detail;
namespace {
template <class T, auto Release> struct Handle {
    T value = nullptr;
    ~Handle() { if (value) Release(value); }
    operator T() const noexcept { return value; }
};
void require(bool condition, std::string_view parameter, std::string_view message) {
    if (!condition) fail(ErrorCode::invalid_argument, "presenter.draw", parameter, message);
}
}

NativeContext native_context(const Context& context) {
    const auto state = Presentation::context(context);
    std::lock_guard lock(state->mutex);
    state->check("native_context");
    return {state->instance, state->adapter, state->device, state->queue};
}

Presenter::Presenter() noexcept = default;
Presenter::~Presenter() = default;
Presenter::Presenter(Presenter&&) noexcept = default;
Presenter& Presenter::operator=(Presenter&&) noexcept = default;
Presenter::Presenter(std::unique_ptr<Presentation> state) : state_(std::move(state)) {}

Presenter Presenter::create(Context& context, WGPUTextureFormat format) try {
    auto owner = require_state(context.state_, "presenter.create");
    std::lock_guard lock(owner->mutex);
    owner->check("presenter.create");
    const bool srgb = format == WGPUTextureFormat_RGBA8UnormSrgb ||
                      format == WGPUTextureFormat_BGRA8UnormSrgb;
    if (!srgb && format != WGPUTextureFormat_RGBA8Unorm && format != WGPUTextureFormat_BGRA8Unorm) {
        fail(ErrorCode::invalid_argument, "presenter.create", "format",
             "display format must be RGBA8 or BGRA8, unorm or sRGB");
    }
    auto state = std::make_unique<Presentation>();
    state->owner = owner;
    state->srgb = srgb;
    state->format = format;
    ErrorScope scope(*owner);
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = sv(present_shader().source);
    WGPUShaderModuleDescriptor shader_desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    shader_desc.nextInChain = &source.chain;
    shader_desc.label = sv("present");
    Handle<WGPUShaderModule, wgpuShaderModuleRelease> shader{
        wgpuDeviceCreateShaderModule(owner->device, &shader_desc)};
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = format;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = shader;
    fragment.entryPoint = sv("fragment");
    fragment.targetCount = 1;
    fragment.targets = &target;
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.label = sv("present");
    descriptor.vertex.module = shader;
    descriptor.vertex.entryPoint = sv("vertex");
    descriptor.fragment = &fragment;
    state->pipeline = wgpuDeviceCreateRenderPipeline(owner->device, &descriptor);
    scope.finish("presenter.create");
    if (!state->pipeline) fail(ErrorCode::execution_failed, "presenter.create", {}, "pipeline creation failed");
    state->layout = wgpuRenderPipelineGetBindGroupLayout(state->pipeline, 0);
    if (!state->layout) fail(ErrorCode::execution_failed, "presenter.create", {}, "pipeline layout unavailable");
    state->uniform = allocate_buffer(*owner, 16, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
                                    "presenter.create");
    return Presenter(std::move(state));
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, "presenter.create", {}, "host allocation failed");
}

Submission Presenter::draw(const Image& image, WGPUTextureView target,
                           std::uint32_t width, std::uint32_t height) try {
    if (!state_) fail(ErrorCode::invalid_resource, "presenter.draw", "presenter", "presenter is not initialized");
    auto& state = *state_->owner;
    std::lock_guard lock(state.mutex);
    state.check("presenter.draw");
    validate_resource(state_->owner, image.resource_, ResourceKind::image, "presenter.draw", "image");
    require(target != nullptr, "target", "target must be a live texture view on this device");
    require(width > 0, "width", "width must be positive");
    require(height > 0, "height", "height must be positive");
    if (width > state.limits.maxTextureDimension2D)
        fail(ErrorCode::capacity, "presenter.draw", "width", "width exceeds device texture limit");
    if (height > state.limits.maxTextureDimension2D)
        fail(ErrorCode::capacity, "presenter.draw", "height", "height exceeds device texture limit");
    if (image.resource_->recorded) {
        fail(ErrorCode::resource_busy, "presenter.draw", "image", "submit recorded image operations before drawing");
    }
    auto flight = std::make_shared<Flight>();
    flight->owner = state_->owner;
    Record record;
    record.source = image.resource_;
    flight->records.push_back(std::move(record));
    state.flights.reserve(state.flights.size() + 1);

    ErrorScope scope(state);
    std::array<WGPUBindGroupEntry, 2> entries{WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
    entries[0].binding = 0;
    entries[0].buffer = image.resource_->buffer;
    entries[0].size = std::uint64_t(image.resource_->width) * image.resource_->height * pixel_bytes;
    entries[1].binding = 1;
    entries[1].buffer = state_->uniform;
    entries[1].size = 16;
    WGPUBindGroupDescriptor group_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    group_desc.layout = state_->layout;
    group_desc.entryCount = entries.size();
    group_desc.entries = entries.data();
    Handle<WGPUBindGroup, wgpuBindGroupRelease> group{wgpuDeviceCreateBindGroup(state.device, &group_desc)};
    WGPUCommandEncoderDescriptor encoder_desc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    Handle<WGPUCommandEncoder, wgpuCommandEncoderRelease> encoder{wgpuDeviceCreateCommandEncoder(state.device, &encoder_desc)};
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = target;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    attachment.clearValue = {0, 0, 0, 0};
    WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &attachment;
    Handle<WGPURenderPassEncoder, wgpuRenderPassEncoderRelease> pass{wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc)};
    wgpuRenderPassEncoderSetPipeline(pass, state_->pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
    wgpuRenderPassEncoderSetViewport(pass, 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1);
    wgpuRenderPassEncoderSetScissorRect(pass, 0, 0, width, height);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    WGPUCommandBufferDescriptor command_desc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
    Handle<WGPUCommandBuffer, wgpuCommandBufferRelease> commands{wgpuCommandEncoderFinish(encoder, &command_desc)};
    scope.finish("presenter.draw");
    if (!commands.value) fail(ErrorCode::execution_failed, "presenter.draw", {}, "render encoding failed");

    flight->uniform = state_->uniform.retain();
    ErrorScope submission_scope(state);
    const std::array<std::uint32_t, 4> dimensions{
        image.resource_->width, image.resource_->height, state_->srgb ? 1u : 0u, 0};
    wgpuQueueWriteBuffer(state.queue, state_->uniform, 0, dimensions.data(), sizeof(dimensions));
    ++image.resource_->pending;
    state.flights.push_back(flight);
    flight->index = submit_queue(state, commands.value);
    WGPUQueueWorkDoneCallbackInfo info = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
    info.mode = WGPUCallbackMode_AllowSpontaneous;
    info.userdata1 = flight.get();
    info.callback = [](WGPUQueueWorkDoneStatus status, WGPUStringView, void* userdata, void*) noexcept {
        auto& out = *static_cast<Flight*>(userdata);
        if (status != WGPUQueueWorkDoneStatus_Success) out.error.store(ErrorCode::execution_failed);
        out.done.store(true);
    };
    flight->completion = wgpuQueueOnSubmittedWorkDone(state.queue, info);
    try { submission_scope.finish("presenter.draw"); }
    catch (const Error& error) { flight->error.store(error.code()); }
    return Submission(std::move(flight));
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, "presenter.draw", {}, "host allocation failed");
}

namespace {
constexpr std::string_view viewport_operation = "presenter.draw";
constexpr std::uint32_t max_levels = 32;

struct ViewUniform {
    std::array<float, 4> inverse, translation;
    std::array<std::uint32_t, 4> info;
    std::array<float, 4> checker, grid, box, background, checker_first, checker_second, overlay,
        grid_color;
    std::array<std::uint32_t, 4> sample;
    std::array<float, 4> sample_scale;
};
static_assert(sizeof(ViewUniform) == 208);

struct ReduceUniform {
    std::array<std::uint32_t, 4> source, target, image;
};

// Level k >= 1 halves level k - 1, rounding up, down to 1 x 1: image word offset (four
// float words per texel), width, height, mask word offset (one float per texel). The
// image levels take about a third of the image's own storage.
struct Levels {
    std::array<std::array<std::uint32_t, 4>, max_levels> table{};
    std::uint32_t count = 0;
    std::uint64_t image_words = 0, mask_words = 0;
};
Levels pyramid_levels(std::uint32_t width, std::uint32_t height) {
    Levels result;
    result.table[0] = {0, width, height, 0};
    while (width > 1 || height > 1) {
        width = (width + 1) / 2;
        height = (height + 1) / 2;
        result.table[++result.count] = {static_cast<std::uint32_t>(result.image_words), width,
                                        height, static_cast<std::uint32_t>(result.mask_words)};
        result.image_words += 4 * std::uint64_t(width) * height;
        result.mask_words += std::uint64_t(width) * height;
    }
    return result;
}

std::array<float, 4> components(Color color) {
    return {color.r, color.g, color.b, color.a};
}

void require_view_color(Color color, std::string_view parameter) {
    require(std::isfinite(color.r) && std::isfinite(color.g) && std::isfinite(color.b) &&
                std::isfinite(color.a) && color.a >= 0 && color.a <= 1,
            parameter, "color must be finite with alpha between zero and one");
}

WGPUBindGroupEntry buffer_entry(std::uint32_t binding, WGPUBuffer buffer, std::uint64_t size,
                                std::uint64_t offset = 0) {
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = binding;
    entry.buffer = buffer;
    entry.offset = offset;
    entry.size = size;
    return entry;
}

void prepare_viewport(Presentation& presentation) {
    if (presentation.viewport) return;
    auto& owner = *presentation.owner;
    auto viewport = std::make_unique<Viewport>();
    ErrorScope scope(owner);
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = sv(present_shader().source);
    WGPUShaderModuleDescriptor shader_desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    shader_desc.nextInChain = &source.chain;
    shader_desc.label = sv("viewport");
    Handle<WGPUShaderModule, wgpuShaderModuleRelease> shader{
        wgpuDeviceCreateShaderModule(owner.device, &shader_desc)};
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = presentation.format;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = shader;
    fragment.entryPoint = sv("viewport_fragment");
    fragment.targetCount = 1;
    fragment.targets = &target;
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.label = sv("viewport");
    descriptor.vertex.module = shader;
    descriptor.vertex.entryPoint = sv("vertex");
    descriptor.fragment = &fragment;
    viewport->pipeline = wgpuDeviceCreateRenderPipeline(owner.device, &descriptor);
    WGPUComputePipelineDescriptor reduce_desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    reduce_desc.label = sv("viewport.reduce");
    reduce_desc.compute.module = shader;
    reduce_desc.compute.entryPoint = sv("reduce_level");
    viewport->reduce = wgpuDeviceCreateComputePipeline(owner.device, &reduce_desc);
    reduce_desc.label = sv("viewport.reduce_axis");
    reduce_desc.compute.entryPoint = sv("reduce_axis");
    viewport->axis = wgpuDeviceCreateComputePipeline(owner.device, &reduce_desc);
    scope.finish(viewport_operation);
    if (!viewport->pipeline || !viewport->reduce || !viewport->axis) {
        fail(ErrorCode::execution_failed, viewport_operation, {}, "pipeline creation failed");
    }
    viewport->layout = wgpuRenderPipelineGetBindGroupLayout(viewport->pipeline, 0);
    viewport->reduce_layout = wgpuComputePipelineGetBindGroupLayout(viewport->reduce, 0);
    viewport->axis_layout = wgpuComputePipelineGetBindGroupLayout(viewport->axis, 0);
    if (!viewport->layout || !viewport->reduce_layout || !viewport->axis_layout) {
        fail(ErrorCode::execution_failed, viewport_operation, {}, "pipeline layout unavailable");
    }
    const std::uint64_t alignment = owner.limits.minUniformBufferOffsetAlignment;
    viewport->reduce_stride = (sizeof(ReduceUniform) + alignment - 1) / alignment * alignment;
    viewport->uniform = allocate_buffer(owner, sizeof(ViewUniform),
                                        WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
                                        viewport_operation);
    viewport->reduce_uniform = allocate_buffer(
        owner, (2 * max_levels + 2) * viewport->reduce_stride,
        WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, viewport_operation);
    viewport->empty = allocate_buffer(owner, 16, WGPUBufferUsage_Storage, viewport_operation);
    presentation.viewport = std::move(viewport);
}

// Records the reduction of source into pyramid levels 1..count; one dispatch per level.
void encode_pyramid(State& owner, Viewport& viewport, WGPUComputePassEncoder pass,
                    Pyramid& pyramid, const Resource& source, const Levels& levels, bool mask,
                    std::vector<ReduceUniform>& uniforms) {
    pyramid.source.reset();
    const auto source_bytes = mask ? mask_bytes(std::uint64_t(source.width) * source.height)
                                   : std::uint64_t(source.width) * source.height * pixel_bytes;
    wgpuComputePassEncoderSetPipeline(pass, viewport.reduce);
    for (std::uint32_t level = 1; level <= levels.count; ++level) {
        const auto& from = levels.table[level - 1];
        const auto& to = levels.table[level];
        const std::uint32_t offset = mask ? 3 : 0;
        const std::uint32_t mode = (mask ? 2 : 0) + (level > 1 ? 1 : 0);
        const auto slot = uniforms.size();
        uniforms.push_back({{from[1], from[2], level > 1 ? from[offset] : 0, mode},
                            {to[1], to[2], to[offset], 0},
                            {source.width, source.height, 1u << (level - 1), 0}});
        const std::array entries{
            buffer_entry(6, source.buffer, source_bytes),
            buffer_entry(7, pyramid.buffer, pyramid.bytes),
            buffer_entry(8, viewport.reduce_uniform, sizeof(ReduceUniform),
                         slot * viewport.reduce_stride)};
        WGPUBindGroupDescriptor group_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        group_desc.layout = viewport.reduce_layout;
        group_desc.entryCount = entries.size();
        group_desc.entries = entries.data();
        Handle<WGPUBindGroup, wgpuBindGroupRelease> group{
            wgpuDeviceCreateBindGroup(owner.device, &group_desc)};
        wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, (to[1] + 7) / 8, (to[2] + 7) / 8, 1);
    }
}

// Anisotropic level: pyramid level `from` (0: the image or mask itself) reduced by factor
// along one axis, from source.buffer or the isotropic pyramid. One bounded dispatch whose
// total reads equal the source level's size.
struct AxisLevel {
    std::uint32_t from = 0, factor = 1, axis = 0, width = 0, height = 0;
};
struct ViewSampling {
    Affine inverse;
    double footprint = 1, box_x = 1, box_y = 1;
    bool minified = false, anisotropic = false;
    Levels levels;
    std::uint32_t level = 0;
    AxisLevel axis;
    ViewportRequirements requirements;
};
ViewSampling sampling_plan(ImageSize source, const ViewportOptions& options,
                           std::string_view operation) {
    if (source.width <= 0 || source.height <= 0 || source.width > INT32_MAX ||
        source.height > INT32_MAX) {
        fail(ErrorCode::invalid_argument, operation, "source", "dimensions must be positive int32");
    }
    if (std::uint64_t(source.width) * source.height > UINT32_MAX) {
        fail(ErrorCode::capacity, operation, "source",
             "pixel count exceeds shader address capacity");
    }
    const auto inverted = inverse(options.view);
    if (!inverted) {
        fail(ErrorCode::invalid_argument, operation, "view", "view must be finite and invertible");
    }
    ViewSampling plan;
    plan.inverse = *inverted;
    const auto& inv = plan.inverse;
    plan.footprint = std::max(std::hypot(double(inv.a), double(inv.b)),
                              std::hypot(double(inv.c), double(inv.d)));
    plan.minified = plan.footprint > 1.0001;
    plan.levels = pyramid_levels(source.width, source.height);
    plan.box_x = std::abs(double(inv.a)) + std::abs(double(inv.c));
    plan.box_y = std::abs(double(inv.b)) + std::abs(double(inv.d));
    const auto level_for = [&](double extent) {
        return static_cast<std::uint32_t>(std::clamp(
            std::floor(std::log2(std::max(extent / 2, 1.0))), 0.0, double(plan.levels.count)));
    };
    const auto short_level = level_for(std::min(plan.box_x, plan.box_y));
    const auto long_level = level_for(std::max(plan.box_x, plan.box_y));
    plan.level = short_level;
    plan.anisotropic = plan.minified && long_level > short_level;
    if (plan.anisotropic) {
        const auto& from = plan.levels.table[short_level];
        auto& axis = plan.axis;
        axis.from = short_level;
        axis.axis = plan.box_y > plan.box_x ? 1 : 0;
        const auto count = axis.axis ? from[2] : from[1];
        const auto factor = std::uint64_t{1} << (long_level - short_level);
        axis.factor = static_cast<std::uint32_t>(std::min<std::uint64_t>(factor, count));
        const auto reduced = (count + axis.factor - 1) / axis.factor;
        axis.width = axis.axis ? from[1] : reduced;
        axis.height = axis.axis ? reduced : from[2];
        const auto pixels = std::uint64_t(axis.width) * axis.height;
        plan.requirements.image_axis = pixels * pixel_bytes;
        if (options.overlay) {
            plan.requirements.mask_axis = pixels * 4;
        }
    }
    if (plan.minified && plan.level > 0) {
        if (plan.levels.image_words > UINT32_MAX) {
            fail(ErrorCode::capacity, operation, "source",
                 "pyramid exceeds shader address capacity");
        }
        plan.requirements.image_pyramid = plan.levels.image_words * 4;
        if (options.overlay) {
            plan.requirements.mask_pyramid = plan.levels.mask_words * 4;
        }
    }
    const auto& need = plan.requirements;
    plan.requirements.total =
        need.image_pyramid + need.mask_pyramid + need.image_axis + need.mask_axis;
    return plan;
}
std::array<std::uint64_t, 4> capacities(const ViewportRequirements& requirements) {
    return {requirements.image_pyramid, requirements.mask_pyramid, requirements.image_axis,
            requirements.mask_axis};
}
std::array<Pyramid*, 4> caches(Viewport& viewport) {
    return {&viewport.image, &viewport.mask, &viewport.image_axis, &viewport.mask_axis};
}
void require_viewport_capacity(Viewport* viewport, const ViewportRequirements& requirements) {
    const auto needed = capacities(requirements);
    const std::array<std::uint64_t, 4> available =
        viewport ? std::array{viewport->image.bytes, viewport->mask.bytes,
                              viewport->image_axis.bytes, viewport->mask_axis.bytes}
                 : std::array<std::uint64_t, 4>{};
    constexpr std::array names{"image pyramid", "mask pyramid", "image axis", "mask axis"};
    for (std::size_t i = 0; i < needed.size(); ++i) {
        if (needed[i] <= available[i]) {
            continue;
        }
        char message[192];
        std::snprintf(message, sizeof(message),
                      "%s requires %llu bytes; %llu reserved; call reserve for these options",
                      names[i], static_cast<unsigned long long>(needed[i]),
                      static_cast<unsigned long long>(available[i]));
        fail(ErrorCode::capacity, viewport_operation, "options", message);
    }
}

void encode_axis(State& owner, Viewport& viewport, WGPUComputePassEncoder pass, Pyramid& cache,
                 const Resource& source, const Pyramid& pyramid, const Levels& levels,
                 const AxisLevel& level, bool mask, std::vector<ReduceUniform>& uniforms) {
    cache.source.reset();
    const auto& from = levels.table[level.from];
    const std::uint32_t mode = (mask ? 2 : 0) + (level.from ? 1 : 0);
    const auto slot = uniforms.size();
    uniforms.push_back({{from[1], from[2], level.from ? from[mask ? 3 : 0] : 0, mode},
                        {level.width, level.height, 0, level.axis},
                        {source.width, source.height, 1u << level.from, level.factor}});
    const auto input = level.from
                           ? buffer_entry(6, pyramid.buffer, pyramid.bytes)
                           : buffer_entry(6, source.buffer,
                                          mask ? mask_bytes(std::uint64_t(source.width) *
                                                            source.height)
                                               : std::uint64_t(source.width) * source.height *
                                                     pixel_bytes);
    const std::array entries{input, buffer_entry(7, cache.buffer, cache.bytes),
                             buffer_entry(8, viewport.reduce_uniform, sizeof(ReduceUniform),
                                          slot * viewport.reduce_stride)};
    WGPUBindGroupDescriptor group_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    group_desc.layout = viewport.axis_layout;
    group_desc.entryCount = entries.size();
    group_desc.entries = entries.data();
    Handle<WGPUBindGroup, wgpuBindGroupRelease> group{
        wgpuDeviceCreateBindGroup(owner.device, &group_desc)};
    wgpuComputePassEncoderSetPipeline(pass, viewport.axis);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, (level.width + 7) / 8, (level.height + 7) / 8,
                                             1);
}
} // namespace

ViewportRequirements viewport_requirements(ImageSize source, const ViewportOptions& options) {
    return sampling_plan(source, options, "viewport_requirements").requirements;
}

void Presenter::reserve(ImageSize source, const ViewportOptions& options) try {
    constexpr std::string_view operation = "presenter.reserve";
    if (!state_) {
        fail(ErrorCode::invalid_resource, operation, "presenter", "presenter is not initialized");
    }
    auto& owner = *state_->owner;
    std::lock_guard lock(owner.mutex);
    owner.check(operation);
    const auto needed = capacities(sampling_plan(source, options, operation).requirements);
    for (auto bytes : needed) {
        if (bytes >
            std::min(owner.limits.maxBufferSize, owner.limits.maxStorageBufferBindingSize)) {
            fail(ErrorCode::capacity, operation, "capacity",
                 "viewport cache exceeds device buffer limits");
        }
    }
    prepare_viewport(*state_);
    const auto slots = caches(*state_->viewport);
    std::array<GpuBuffer, 4> replacements;
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (needed[i] > slots[i]->bytes) {
            replacements[i] = allocate_buffer(owner, needed[i], WGPUBufferUsage_Storage, operation,
                                              MemoryCategory::presentation);
        }
    }
    // Commit only after every new allocation succeeds. Pending submissions retain
    // replaced buffers; unchanged slots retain their valid cached pixels.
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (replacements[i]) {
            slots[i]->buffer = std::move(replacements[i]);
            slots[i]->bytes = needed[i];
            slots[i]->source.reset();
            slots[i]->generation.reset();
        }
    }
} catch (const Error& error) {
    throw Error(error.code(), "presenter.reserve", error.parameter(), error.what());
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, "presenter.reserve", "capacity", "host allocation failed");
}

Submission Presenter::draw(const Image& image, WGPUTextureView target, std::uint32_t width,
                           std::uint32_t height, const ViewportOptions& options) try {
    if (!state_) fail(ErrorCode::invalid_resource, viewport_operation, "presenter", "presenter is not initialized");
    auto& state = *state_->owner;
    std::lock_guard lock(state.mutex);
    state.check(viewport_operation);
    validate_resource(state_->owner, image.resource_, ResourceKind::image, viewport_operation, "image");
    require(target != nullptr, "target", "target must be a live texture view on this device");
    require(width > 0, "width", "width must be positive");
    require(height > 0, "height", "height must be positive");
    if (width > state.limits.maxTextureDimension2D)
        fail(ErrorCode::capacity, "presenter.draw", "width", "width exceeds device texture limit");
    if (height > state.limits.maxTextureDimension2D)
        fail(ErrorCode::capacity, "presenter.draw", "height", "height exceeds device texture limit");
    const auto view = options.view;
    const auto inverse_view = inverse(view);
    require(std::isfinite(view.a) && std::isfinite(view.b) && std::isfinite(view.c) &&
                std::isfinite(view.d) && std::isfinite(view.e) && std::isfinite(view.f) &&
                inverse_view.has_value(),
            "view", "view must be finite and invertible");
    require_view_color(options.background, "background");
    require_view_color(options.checker_first, "checker_first");
    require_view_color(options.checker_second, "checker_second");
    require_view_color(options.overlay_color, "overlay_color");
    require_view_color(options.pixel_grid_color, "pixel_grid_color");
    require(std::isfinite(options.pixel_grid_zoom) && options.pixel_grid_zoom >= 0,
            "pixel_grid_zoom", "pixel grid zoom must be finite and nonnegative");
    const auto& pixels = image.resource_;
    std::shared_ptr<Resource> mask;
    if (options.overlay) {
        mask = options.overlay->resource_;
        validate_resource(state_->owner, mask, ResourceKind::mask, viewport_operation, "overlay");
        require(mask->width == pixels->width && mask->height == pixels->height, "overlay",
                "overlay mask must match the image size");
    }
    if (pixels->recorded) {
        fail(ErrorCode::resource_busy, viewport_operation, "image", "submit recorded image operations before drawing");
    }
    if (mask && mask->recorded) {
        fail(ErrorCode::resource_busy, viewport_operation, "overlay", "submit recorded mask operations before drawing");
    }
    const auto plan = sampling_plan({pixels->width, pixels->height}, options, viewport_operation);
    require_viewport_capacity(state_->viewport.get(), plan.requirements);
    prepare_viewport(*state_);
    auto& viewport = *state_->viewport;
    const auto& inv = plan.inverse;
    const auto footprint = plan.footprint;
    const bool minified = plan.minified, anisotropic = plan.anisotropic;
    const auto& levels = plan.levels;
    const auto box_x = plan.box_x, box_y = plan.box_y;
    const auto level = plan.level;
    const auto& axis_level = plan.axis;
    ViewUniform uniform{};
    uniform.inverse = {inv.a, inv.b, inv.c, inv.d};
    uniform.translation = {inv.e, inv.f, 0, float(1 / footprint)};
    uniform.box = {float(box_x / 2), float(box_y / 2), minified ? 1.0f : 0.0f, 0};
    uniform.info = {pixels->width, pixels->height, 0,
                    (state_->srgb ? 1u : 0u) | (mask ? 2u : 0u) |
                        (options.overlay_selected ? 4u : 0u) | (options.pixel_grid ? 8u : 0u)};
    // Cells align with target pixels: the anchor is the canvas origin, rounded.
    uniform.checker = {std::round(view.e), std::round(view.f), float(options.checker_size),
                       options.pixel_grid_zoom};
    uniform.grid = {float(std::hypot(double(inv.a), double(inv.c))),
                    float(std::hypot(double(inv.b), double(inv.d))), 0, 0};
    uniform.background = components(options.background);
    uniform.checker_first = components(options.checker_first);
    uniform.checker_second = components(options.checker_second);
    uniform.overlay = components(options.overlay_color);
    uniform.grid_color = components(options.pixel_grid_color);
    const auto texel = static_cast<float>(std::uint64_t{1} << level);
    if (anisotropic) {
        const float stretched = texel * float(axis_level.factor);
        uniform.sample = {0, axis_level.width, axis_level.height, 0};
        uniform.sample_scale = {axis_level.axis ? texel : stretched,
                                axis_level.axis ? stretched : texel, 0, 0};
    } else {
        const auto& table = levels.table[level];
        uniform.sample = {table[0], table[1], table[2], table[3]};
        uniform.sample_scale = {texel, texel, level ? 0.0f : 1.0f, 0};
    }

    auto flight = std::make_shared<Flight>();
    flight->owner = state_->owner;
    Record record;
    record.source = pixels;
    flight->records.push_back(std::move(record));
    if (mask) {
        Record coverage;
        coverage.source = mask;
        flight->records.push_back(std::move(coverage));
    }
    state.flights.reserve(state.flights.size() + 1);

    ErrorScope scope(state);
    WGPUCommandEncoderDescriptor encoder_desc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    Handle<WGPUCommandEncoder, wgpuCommandEncoderRelease> encoder{wgpuDeviceCreateCommandEncoder(state.device, &encoder_desc)};
    std::vector<ReduceUniform> reductions;
    // The isotropic pyramid serves isotropic levels above 0 and axis levels built from one.
    const bool use_pyramid = minified && level > 0;
    const std::array<std::uint32_t, 3> axis_key{axis_level.from, axis_level.factor,
                                                axis_level.axis};
    const bool rebuild_image = use_pyramid && !viewport.image.current(pixels);
    const bool rebuild_mask = use_pyramid && mask && !viewport.mask.current(mask);
    const bool rebuild_image_axis =
        anisotropic && (rebuild_image || !viewport.image_axis.current(pixels, axis_key));
    const bool rebuild_mask_axis =
        anisotropic && mask && (rebuild_mask || !viewport.mask_axis.current(mask, axis_key));
    if (rebuild_image || rebuild_mask || rebuild_image_axis || rebuild_mask_axis) {
        WGPUComputePassDescriptor pass_desc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        Handle<WGPUComputePassEncoder, wgpuComputePassEncoderRelease> pass{
            wgpuCommandEncoderBeginComputePass(encoder, &pass_desc)};
        if (rebuild_image) {
            encode_pyramid(state, viewport, pass, viewport.image, *pixels, levels, false, reductions);
        }
        if (rebuild_mask) {
            encode_pyramid(state, viewport, pass, viewport.mask, *mask, levels, true, reductions);
        }
        if (rebuild_image_axis) {
            encode_axis(state, viewport, pass, viewport.image_axis, *pixels, viewport.image, levels,
                        axis_level, false, reductions);
        }
        if (rebuild_mask_axis) {
            encode_axis(state, viewport, pass, viewport.mask_axis, *mask, viewport.mask, levels,
                        axis_level, true, reductions);
        }
        wgpuComputePassEncoderEnd(pass);
    }
    const auto& image_source = anisotropic ? viewport.image_axis : viewport.image;
    const auto& mask_source = anisotropic ? viewport.mask_axis : viewport.mask;
    const bool sampled = anisotropic || use_pyramid;
    const std::array entries{
        buffer_entry(0, pixels->buffer, std::uint64_t(pixels->width) * pixels->height * pixel_bytes),
        buffer_entry(2, viewport.uniform, sizeof(ViewUniform)),
        sampled ? buffer_entry(3, image_source.buffer, image_source.bytes)
                : buffer_entry(3, viewport.empty, 16),
        mask ? buffer_entry(4, mask->buffer, mask_bytes(std::uint64_t(mask->width) * mask->height))
             : buffer_entry(4, viewport.empty, 16),
        sampled && mask ? buffer_entry(5, mask_source.buffer, mask_source.bytes)
                        : buffer_entry(5, viewport.empty, 16)};
    WGPUBindGroupDescriptor group_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    group_desc.layout = viewport.layout;
    group_desc.entryCount = entries.size();
    group_desc.entries = entries.data();
    Handle<WGPUBindGroup, wgpuBindGroupRelease> group{wgpuDeviceCreateBindGroup(state.device, &group_desc)};
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = target;
    attachment.loadOp = WGPULoadOp_Clear;
    attachment.storeOp = WGPUStoreOp_Store;
    attachment.clearValue = {0, 0, 0, 0};
    WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &attachment;
    Handle<WGPURenderPassEncoder, wgpuRenderPassEncoderRelease> pass{wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc)};
    wgpuRenderPassEncoderSetPipeline(pass, viewport.pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
    wgpuRenderPassEncoderSetViewport(pass, 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1);
    wgpuRenderPassEncoderSetScissorRect(pass, 0, 0, width, height);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    WGPUCommandBufferDescriptor command_desc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
    Handle<WGPUCommandBuffer, wgpuCommandBufferRelease> commands{wgpuCommandEncoderFinish(encoder, &command_desc)};
    scope.finish(viewport_operation);
    if (!commands.value) fail(ErrorCode::execution_failed, viewport_operation, {}, "render encoding failed");

    flight->buffers.reserve(7);
    for (const auto* buffer : {&viewport.uniform, &viewport.reduce_uniform, &viewport.empty,
                               &viewport.image.buffer, &viewport.mask.buffer,
                               &viewport.image_axis.buffer, &viewport.mask_axis.buffer}) {
        if (*buffer) flight->buffers.push_back(buffer->retain());
    }
    ErrorScope submission_scope(state);
    wgpuQueueWriteBuffer(state.queue, viewport.uniform, 0, &uniform, sizeof(uniform));
    for (std::size_t slot = 0; slot < reductions.size(); ++slot) {
        wgpuQueueWriteBuffer(state.queue, viewport.reduce_uniform, slot * viewport.reduce_stride,
                             &reductions[slot], sizeof(ReduceUniform));
    }
    for (const auto& retained : flight->records) {
        ++retained.source->pending;
    }
    state.flights.push_back(flight);
    flight->index = submit_queue(state, commands.value);
    for (auto [pyramid, resource, rebuilt, key] :
         {std::tuple{&viewport.image, pixels, rebuild_image, std::array<std::uint32_t, 3>{}},
          std::tuple{&viewport.mask, mask, rebuild_mask, std::array<std::uint32_t, 3>{}},
          std::tuple{&viewport.image_axis, pixels, rebuild_image_axis, axis_key},
          std::tuple{&viewport.mask_axis, mask, rebuild_mask_axis, axis_key}}) {
        if (rebuilt) {
            pyramid->source = resource;
            pyramid->generation = flight;
            pyramid->revision = resource->revision();
            pyramid->width = resource->width;
            pyramid->height = resource->height;
            pyramid->key = key;
        }
    }
    WGPUQueueWorkDoneCallbackInfo info = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
    info.mode = WGPUCallbackMode_AllowSpontaneous;
    info.userdata1 = flight.get();
    info.callback = [](WGPUQueueWorkDoneStatus status, WGPUStringView, void* userdata, void*) noexcept {
        auto& out = *static_cast<Flight*>(userdata);
        if (status != WGPUQueueWorkDoneStatus_Success) out.error.store(ErrorCode::execution_failed);
        out.done.store(true);
    };
    flight->completion = wgpuQueueOnSubmittedWorkDone(state.queue, info);
    try { submission_scope.finish(viewport_operation); }
    catch (const Error& error) { flight->error.store(error.code()); }
    return Submission(std::move(flight));
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, viewport_operation, {}, "host allocation failed");
}
} // namespace wgpupixel::webgpu

namespace wgpupixel::detail {
struct DisplayState {
    Context context;
    webgpu::Presenter presenter;
    Submission last;
    bool pending = false;
    GpuTexture texture;
    WGPUTextureView view = nullptr;
    std::uint32_t width = 0, height = 0;
    ~DisplayState() {
        if (pending) { try { context.wait(last); } catch (...) {} }
        if (view) wgpuTextureViewRelease(view);
    }
};
}

namespace wgpupixel::webgpu {
Display::Display() noexcept = default;
Display::~Display() = default;
Display::Display(Display&&) noexcept = default;
Display& Display::operator=(Display&&) noexcept = default;
Display::Display(std::unique_ptr<detail::DisplayState> state) : state_(std::move(state)) {}

Display Display::create(Context& context, std::uint32_t width, std::uint32_t height) try {
    const auto owner = detail::require_state(context.state_, "display.create");
    auto state = std::make_unique<detail::DisplayState>();
    state->context = Context(owner);
    {
        std::lock_guard lock(owner->mutex);
        owner->check("display.create");
        if (!width) fail(ErrorCode::invalid_argument, "display.create", "width", "width must be positive");
        if (!height) fail(ErrorCode::invalid_argument, "display.create", "height", "height must be positive");
        if (width > owner->limits.maxTextureDimension2D)
            fail(ErrorCode::capacity, "display.create", "width", "width exceeds device texture limit");
        if (height > owner->limits.maxTextureDimension2D)
            fail(ErrorCode::capacity, "display.create", "height", "height exceeds device texture limit");
        detail::ErrorScope errors(*owner);
        state->texture = allocate_texture(*owner, width, height, "display.create");
        if (state->texture) state->view = wgpuTextureCreateView(state->texture, nullptr);
        errors.finish("display.create");
        if (!state->texture || !state->view)
            detail::fail(ErrorCode::out_of_memory, "display.create", {}, "display allocation failed");
    }
    state->presenter = Presenter::create(state->context, WGPUTextureFormat_RGBA8Unorm);
    state->width = width;
    state->height = height;
    return Display(std::move(state));
} catch (const Error& error) {
    throw Error(error.code(), "display.create", error.parameter(), error.what());
} catch (const std::bad_alloc&) {
    detail::fail(ErrorCode::out_of_memory, "display.create", {}, "host allocation failed");
}

void Display::reserve(ImageSize source, const ViewportOptions& options) try {
    if (!state_) {
        fail(ErrorCode::invalid_resource, "display.reserve", "display", "display is closed");
    }
    state_->presenter.reserve(source, options);
} catch (const Error& error) {
    throw Error(error.code(), "display.reserve", error.parameter(), error.what());
}

Submission Display::draw(const Image& image) try {
    if (!state_) detail::fail(ErrorCode::invalid_resource, "display.draw", "display", "display is closed");
    state_->last = state_->presenter.draw(image, state_->view, state_->width, state_->height);
    state_->pending = true;
    return state_->last;
} catch (const Error& error) {
    throw Error(error.code(), "display.draw", error.parameter(), error.what());
}

Submission Display::draw(const Image& image, const ViewportOptions& options) try {
    if (!state_) detail::fail(ErrorCode::invalid_resource, "display.draw", "display", "display is closed");
    state_->last = state_->presenter.draw(image, state_->view, state_->width, state_->height, options);
    state_->pending = true;
    return state_->last;
} catch (const Error& error) {
    throw Error(error.code(), "display.draw", error.parameter(), error.what());
}

void Display::wait() {
    if (!state_) detail::fail(ErrorCode::invalid_resource, "display.wait", "display", "display is closed");
    if (state_->pending) {
        state_->context.wait(state_->last);
        state_->pending = false;
    }
}

void Display::close() {
    if (!state_) return;
    wait();
    state_.reset();
}

WGPUTextureView Display::view() const {
    if (!state_) detail::fail(ErrorCode::invalid_resource, "display.view", "display", "display is closed");
    return state_->view;
}
}
