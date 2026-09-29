#include "test.h"
#include <wgpupixel_webgpu.h>
#include <webgpu/wgpu.h>
#include <array>
#include <utility>
#include <vector>
#include <thread>
#include <atomic>
#include "../src/runtime.h"

namespace {
WGPUInstance observed = nullptr;
bool fail_buffer = false;
bool fail_encoder = false;
bool fail_completion = false;
unsigned fail_buffer_after = 0;
unsigned fail_encoder_after = 0;
unsigned fail_pipeline_after = 0;
std::uint64_t allocated_bytes = 0, buffer_calls = 0;
std::uint64_t copy_calls = 0, copied_bytes = 0;

WGPUHubReport report() {
    WGPUGlobalReport result{};
    wgpuGenerateReport(observed, &result);
    return result.hub;
}

void same_live_resources(const WGPUHubReport& actual, const WGPUHubReport& expected) {
    const std::array registries = {
        std::pair{"buffers", &WGPUHubReport::buffers},
        std::pair{"bind groups", &WGPUHubReport::bindGroups},
        std::pair{"command buffers", &WGPUHubReport::commandBuffers},
        std::pair{"shaders", &WGPUHubReport::shaderModules},
        std::pair{"pipelines", &WGPUHubReport::computePipelines},
        std::pair{"pipeline layouts", &WGPUHubReport::pipelineLayouts},
        std::pair{"binding layouts", &WGPUHubReport::bindGroupLayouts},
        std::pair{"adapters", &WGPUHubReport::adapters},
        std::pair{"devices", &WGPUHubReport::devices},
        std::pair{"queues", &WGPUHubReport::queues},
    };
    for (auto [name, member] : registries) {
        const auto& a = actual.*member;
        const auto& e = expected.*member;
        test::check(a.numKeptFromUser == e.numKeptFromUser && a.numAllocated == e.numAllocated,
                    std::string("native resources were not released: ") + name);
    }
}
} // namespace

// The test retains only the instance, allowing inspection after Context teardown.
extern "C" WGPUInstance __real_wgpuCreateInstance(const WGPUInstanceDescriptor*);
extern "C" WGPUInstance __wrap_wgpuCreateInstance(const WGPUInstanceDescriptor* descriptor) {
    observed = __real_wgpuCreateInstance(descriptor);
    if (observed) {
        wgpuInstanceAddRef(observed);
    }
    return observed;
}

extern "C" WGPUBuffer __real_wgpuDeviceCreateBuffer(WGPUDevice, const WGPUBufferDescriptor*);
extern "C" WGPUBuffer __wrap_wgpuDeviceCreateBuffer(WGPUDevice device,
                                                    const WGPUBufferDescriptor* descriptor) {
    ++buffer_calls;
    if (std::exchange(fail_buffer, false) || (fail_buffer_after && --fail_buffer_after == 0)) {
        return nullptr;
    }
    allocated_bytes += descriptor->size;
    return __real_wgpuDeviceCreateBuffer(device, descriptor);
}

extern "C" WGPUCommandEncoder
__real_wgpuDeviceCreateCommandEncoder(WGPUDevice, const WGPUCommandEncoderDescriptor*);
extern "C" WGPUCommandEncoder
__wrap_wgpuDeviceCreateCommandEncoder(WGPUDevice device,
                                      const WGPUCommandEncoderDescriptor* descriptor) {
    return (std::exchange(fail_encoder, false) ||
            (fail_encoder_after && --fail_encoder_after == 0))
               ? nullptr
               : __real_wgpuDeviceCreateCommandEncoder(device, descriptor);
}

extern "C" void __real_wgpuCommandEncoderCopyBufferToBuffer(
    WGPUCommandEncoder, WGPUBuffer, std::uint64_t, WGPUBuffer, std::uint64_t, std::uint64_t);
extern "C" void __wrap_wgpuCommandEncoderCopyBufferToBuffer(
    WGPUCommandEncoder encoder, WGPUBuffer source, std::uint64_t source_offset,
    WGPUBuffer destination, std::uint64_t destination_offset, std::uint64_t size) {
    ++copy_calls;
    copied_bytes += size;
    __real_wgpuCommandEncoderCopyBufferToBuffer(encoder, source, source_offset, destination,
                                                destination_offset, size);
}

extern "C" WGPUComputePipeline
__real_wgpuDeviceCreateComputePipeline(WGPUDevice, const WGPUComputePipelineDescriptor*);
extern "C" WGPUComputePipeline
__wrap_wgpuDeviceCreateComputePipeline(WGPUDevice device,
                                       const WGPUComputePipelineDescriptor* descriptor) {
    if (fail_pipeline_after && --fail_pipeline_after == 0) {
        return nullptr;
    }
    return __real_wgpuDeviceCreateComputePipeline(device, descriptor);
}

extern "C" WGPUFuture __real_wgpuQueueOnSubmittedWorkDone(WGPUQueue, WGPUQueueWorkDoneCallbackInfo);
extern "C" WGPUFuture __wrap_wgpuQueueOnSubmittedWorkDone(WGPUQueue queue,
                                                          WGPUQueueWorkDoneCallbackInfo info) {
    if (std::exchange(fail_completion, false)) {
        info.callback(WGPUQueueWorkDoneStatus_Error, {nullptr, 0}, info.userdata1, info.userdata2);
        return {};
    }
    return __real_wgpuQueueOnSubmittedWorkDone(queue, info);
}

namespace {
void accounting(wgpupixel::Context& ctx) {
    using namespace wgpupixel;
    const auto native_baseline = report().buffers.numKeptFromUser;
    std::uint64_t live_buffers = 0, tracked_allocations = 0;
    auto calls = buffer_calls;
    MemoryUsage expected{};
    const auto check = [&] {
        const auto actual = ctx.memory();
        expected.total = expected.images + expected.masks + expected.transfers + expected.internal +
                         expected.presentation + expected.workspace;
        expected.peak = std::max(expected.peak, expected.total);
        test::check(actual.images == expected.images && actual.masks == expected.masks &&
                        actual.transfers == expected.transfers &&
                        actual.internal == expected.internal &&
                        actual.presentation == expected.presentation &&
                        actual.workspace == expected.workspace && actual.total == expected.total &&
                        actual.peak == expected.peak,
                    "requested allocation bytes/category/peak differ from ledger");
        test::check(report().buffers.numKeptFromUser == native_baseline + live_buffers,
                    "ledger-tracked live allocations differ from native buffer count");
        test::check(buffer_calls - calls == tracked_allocations,
                    "wrapped CreateBuffer count must equal ledger-tracked allocations");
    };
    const auto reset = [&] {
        ctx.reset_peak();
        expected.peak = expected.total;
    };
    reset();
    check();
    constexpr std::uint64_t pixels = 17 * 9, mask_size = 156;
    auto image = ctx.create_image({17, 9});
    expected.images += pixels * 16; ++live_buffers; ++tracked_allocations; check();
    auto alias = image;
    image.set_size({9, 9}); check();
    image.set_size({17, 9}); check();
    auto mask = ctx.create_mask({17, 9});
    expected.masks += mask_size; ++live_buffers; ++tracked_allocations; check();
    for (auto format : {TransferFormat::rgba8, TransferFormat::rgba16, TransferFormat::rgba32_float}) {
        const auto size = pixels * (format == TransferFormat::rgba8 ? 4 :
                                   format == TransferFormat::rgba16 ? 8 : 16);
        auto upload = ctx.create_upload_buffer(image, {.format = format});
        expected.transfers += size; ++live_buffers; ++tracked_allocations; check();
        auto readback = ctx.create_readback_buffer(image, {.format = format});
        expected.transfers += 2 * size; live_buffers += 2; tracked_allocations += 2; check();
        ctx.destroy(upload); expected.transfers -= size; --live_buffers; check();
        ctx.destroy(readback); expected.transfers -= 2 * size; live_buffers -= 2; check();
    }
    auto upload = ctx.create_upload_buffer(mask);
    expected.transfers += mask_size; ++live_buffers; ++tracked_allocations; check();
    auto readback = ctx.create_readback_buffer(mask);
    expected.transfers += 2 * mask_size; live_buffers += 2; tracked_allocations += 2; check();
    ctx.destroy(upload); expected.transfers -= mask_size; --live_buffers; check();
    ctx.destroy(readback); expected.transfers -= 2 * mask_size; live_buffers -= 2; check();
    auto histogram = ctx.create_histogram_buffer(17);
    constexpr auto histogram_size = 2 * 17 * analysis_channel_count * 4;
    expected.internal += histogram_size; live_buffers += 2; tracked_allocations += 2; check();
    auto statistics = ctx.create_statistics_buffer();
    constexpr auto statistics_size = 2 * 2048 * 40 * 4;
    expected.internal += statistics_size; live_buffers += 2; tracked_allocations += 2; check();
    auto bounds = ctx.create_mask_bounds_buffer();
    constexpr auto bounds_size = 2 * 16;
    expected.internal += bounds_size; live_buffers += 2; tracked_allocations += 2; check();
    ctx.destroy(histogram); expected.internal -= histogram_size; live_buffers -= 2; check();
    ctx.destroy(statistics); expected.internal -= statistics_size; live_buffers -= 2; check();
    ctx.destroy(bounds); expected.internal -= bounds_size; live_buffers -= 2; check();
    WGPULimits limits = WGPU_LIMITS_INIT;
    wgpuDeviceGetLimits(webgpu::native_context(ctx).device, &limits);
    const auto alignment = limits.minUniformBufferOffsetAlignment;
    const auto stride = (sizeof(detail::Parameters) + alignment - 1) / alignment * alignment;
    auto commands = ctx.create_commands(8);
    expected.internal += 8 * stride; ++live_buffers; ++tracked_allocations; check();
    std::array<StrokeSample, 1> point{{{{8, 4}}}};
    commands.brush_stroke(image, {.samples = point,
                                  .brush = {.diameter = 4, .spacing = 0},
                                  .color = {-1, 2, 0, 0.000001f},
                                  .mask = &mask,
                                  .region = Rect{1, 1, 12, 7}});
    const auto flight = ctx.submit(commands);
    expected.internal += 44;
    ++live_buffers;
    ++tracked_allocations;
    check();
    commands = {};
    check(); // Flight alone owns the uniforms and data now.
    ctx.wait(flight);
    expected.internal -= 8 * stride + 44;
    live_buffers -= 2;
    check();
    ctx.destroy(mask); expected.masks = 0; --live_buffers; check();
    ctx.destroy(image); expected.images = 0; --live_buffers; check();
    reset(); check();

    // Hard boundary, lowering below usage, and an allocation freed through an alias.
    ctx.set_memory_limit(16);
    image = ctx.create_image({1, 1});
    expected.images = 16; ++live_buffers; ++tracked_allocations; check();
    ctx.set_memory_limit(1);
    test::error(ErrorCode::capacity, "create_image", "memory_limit",
                [&] { (void)ctx.create_image({1, 1}); });
    check(); // Rejected before the wrapped driver entry point.
    ctx.set_memory_limit(31);
    test::error(ErrorCode::capacity, "create_image", "memory_limit",
                [&] { (void)ctx.create_image({1, 1}); });
    check();
    alias = image;
    ctx.destroy(alias); expected.images = 0; --live_buffers; check();
    image = ctx.create_image({1, 1});
    expected.images = 16; ++live_buffers; ++tracked_allocations; check();
    ctx.destroy(image); expected.images = 0; --live_buffers; check();
    ctx.set_memory_limit(0);
    reset();

    // Reject the second readback allocation; rollback frees the first allocation.
    image = ctx.create_image({17, 9});
    expected.images = pixels * 16; ++live_buffers; ++tracked_allocations; check();
    ctx.set_memory_limit(expected.total + pixels * 4);
    test::error(ErrorCode::capacity, "create_readback_buffer", "memory_limit",
                [&] { (void)ctx.create_readback_buffer(image); });
    ++tracked_allocations;
    expected.peak = expected.total + pixels * 4;
    check();
    ctx.set_memory_limit(0);
    ctx.destroy(image); expected.images = 0; --live_buffers; check();
    reset();

    // Allocation failure at submit keeps the staged recording retryable.
    image = ctx.create_image({17, 9});
    expected.images = pixels * 16; ++live_buffers; ++tracked_allocations; check();
    commands = ctx.create_commands(1);
    expected.internal = stride; ++live_buffers; ++tracked_allocations; check();
    commands.brush_stroke(
        image,
        {.samples = point, .brush = {.diameter = 4, .spacing = 0}, .color = {1, 0, 0, 0.000001f}});
    ctx.set_memory_limit(expected.total + 43);
    test::error(ErrorCode::capacity, "submit", "memory_limit", [&] { (void)ctx.submit(commands); });
    check();
    ctx.set_memory_limit(expected.total + 44);
    ctx.submit_and_wait(commands);
    expected.internal += 44;
    ++live_buffers;
    ++tracked_allocations;
    check();
    std::array<StrokeSample, 2> two_points{{{{8, 4}}, {{9, 4}}}};
    commands.brush_stroke(
        image,
        {.samples = two_points, .brush = {.diameter = 4, .spacing = 0}, .color = {1, 0, 0, 1}});
    ctx.set_memory_limit(expected.total + 87);
    test::error(ErrorCode::capacity, "submit", "memory_limit", [&] { (void)ctx.submit(commands); });
    check();
    ctx.set_memory_limit(expected.total + 88);
    ctx.submit_and_wait(commands);
    expected.peak = expected.total + 88; // New and old data storage overlap during growth.
    expected.internal += 44;
    ++tracked_allocations;
    check();
    commands = {}; expected.internal = 0; live_buffers -= 2; check();
    ctx.destroy(image); expected.images = 0; --live_buffers; check();
    ctx.set_memory_limit(0);
    reset();

    // Display owns exactly RGBA8 texels, plus a 16-byte internal presenter uniform.
    auto display = webgpu::Display::create(ctx, 17, 9);
    expected.presentation = pixels * 4; expected.internal = 16;
    ++live_buffers; ++tracked_allocations; check();
    display.close(); expected.presentation = expected.internal = 0; --live_buffers; check();
    reset();
    ctx.set_memory_limit(pixels * 4 - 1);
    test::error(ErrorCode::capacity, "display.create", "memory_limit",
                [&] { (void)webgpu::Display::create(ctx, 17, 9); });
    check();
    ctx.set_memory_limit(pixels * 4);
    test::error(ErrorCode::capacity, "display.create", "memory_limit",
                [&] { (void)webgpu::Display::create(ctx, 17, 9); });
    expected.peak = pixels * 4; check();
    ctx.set_memory_limit(0);
    reset();

    // Resize reductions use the caller's workspace; recording allocates nothing.
    image = ctx.create_image({1024, 512});
    auto output = ctx.create_image({9, 9});
    commands = ctx.create_commands(4);
    const auto persistent = ctx.memory();
    const auto before_bytes = allocated_bytes, before_calls = buffer_calls;
    commands.fill(image, {.color = {-1, 2, 0, 1}});
    const ResizeOptions area{.filter = ResizeFilter::area};
    const auto plan = resize_requirements(image.size(), output.size(), area).workspace;
    test::check(plan.bytes() == 9 * 512 * 16, "the separable intermediate is 9 x 512 float pixels");
    test::error(ErrorCode::capacity, "resize", "workspace",
                [&] { commands.resize(image, output, area); });
    test::check(ctx.memory().total == persistent.total && buffer_calls == before_calls,
                "a missing workspace must leave recording and allocations unchanged");
    ctx.set_memory_limit(persistent.total + plan.bytes() - 1);
    test::error(ErrorCode::capacity, "create_workspace", "memory_limit",
                [&] { (void)ctx.create_workspace(plan); });
    ctx.set_memory_limit(0);
    auto workspace = ctx.create_workspace(plan);
    auto with = area;
    with.workspace = workspace;
    commands.resize(image, output, with);
    const auto recorded = ctx.memory();
    test::check(buffer_calls == before_calls + 1 && recorded.workspace == plan.bytes() &&
                    recorded.internal == persistent.internal &&
                    allocated_bytes - before_bytes == plan.bytes() &&
                    recorded.images == persistent.images,
                "the workspace must be the only allocation, and recording adds none");
    const auto resized = ctx.submit(commands);
    commands = {};
    workspace = {};
    with = {};
    test::check(ctx.memory().workspace == plan.bytes(), "pending work retains the workspace");
    ctx.wait(resized);
    test::check(ctx.memory().internal == 0 && ctx.memory().workspace == 0 &&
                    ctx.memory().peak == recorded.total,
                "retirement releases the last owner of the workspace and flight uniforms");
    ctx.destroy(output);
    ctx.destroy(image);
    test::check(ctx.memory().total == 0, "geometry resources did not return to zero");

    // Explicit filter workspace is retained by pending records, then released.
    image = ctx.create_image({33, 17});
    commands = ctx.create_commands(32);
    auto start = ctx.memory();
    auto bytes = allocated_bytes;
    workspace = ctx.create_workspace(
        gaussian_blur_requirements(image.size(), {.radius = 256, .sigma = 32}).workspace);
    commands.gaussian_blur(image, {.radius = 256, .sigma = 32, .workspace = workspace});
    constexpr auto filter_bytes = (17 * 9 + 3 * 9 * 5) * 16;
    test::check(ctx.memory().workspace - start.workspace == filter_bytes &&
                    allocated_bytes - bytes == filter_bytes,
                "filter pyramid and scratch requested bytes differ from ledger");
    const auto blurred = ctx.submit(commands);
    const auto pending = ctx.memory();
    workspace = {};
    commands = {};
    test::check(ctx.memory().total == pending.total, "filter workspace released before wait");
    ctx.wait(blurred);
    test::check(ctx.memory().workspace == 0, "retired filter workspace was not released");
    ctx.destroy(image);
    test::check(ctx.memory().total == 0, "filter resources did not return to zero");

    // Viewport uniform buffers, empty binding, image/mask pyramids and axis caches.
    image = ctx.create_image({33, 17});
    mask = ctx.create_mask({33, 17});
    display = webgpu::Display::create(ctx, 9, 9);
    start = ctx.memory(); bytes = allocated_bytes;
    const auto viewport_calls = buffer_calls;
    const webgpu::ViewportOptions viewport_options{.view = Affine{.a = 0.0625f, .d = 0.25f},
                                                   .overlay = &mask};
    display.reserve(image.size(), viewport_options);
    display.draw(image, viewport_options);
    const auto reduce_stride = (48 + alignment - 1) / alignment * alignment;
    const auto viewport_internal = 208 + 66 * reduce_stride + 16;
    const auto viewport_cache = (222 + 45) * 20;
    const auto viewport_bytes = viewport_internal + viewport_cache;
    test::check(ctx.memory().internal - start.internal == viewport_internal &&
                    ctx.memory().presentation - start.presentation == viewport_cache &&
                    allocated_bytes - bytes == viewport_bytes && buffer_calls - viewport_calls == 7,
                "viewport uniforms, image/mask pyramids and axis caches differ from ledger");
    display.wait();
    {
        auto presenter = webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA8Unorm);
        const auto base = ctx.memory();
        presenter.reserve(image.size(), viewport_options);
        auto drawn = presenter.draw(image, display.view(), 9, 9, viewport_options);
        presenter = {};
        test::check(ctx.memory().internal == base.internal - 16 + viewport_internal &&
                        ctx.memory().presentation == base.presentation + viewport_cache,
                    "Presenter destruction must retain pending viewport allocations");
        ctx.wait(drawn);
        test::check(ctx.memory().internal == base.internal - 16 &&
                        ctx.memory().presentation == base.presentation,
                    "retirement must release flight-owned viewport allocations");
        presenter = webgpu::Presenter::create(ctx, WGPUTextureFormat_RGBA8Unorm);
        drawn = presenter.draw(image, display.view(), 9, 9);
        presenter = {};
        test::check(ctx.memory().internal == base.internal,
                    "Presenter destruction must retain the pending basic uniform");
        ctx.wait(drawn);
        test::check(ctx.memory().internal == base.internal - 16,
                    "retirement must release the basic presenter uniform");
    }
    display.close();
    test::check(ctx.memory().internal == 0 && ctx.memory().presentation == 0,
                "closing display must release viewport caches and texture");
    ctx.destroy(mask); ctx.destroy(image);
    test::check(ctx.memory().total == 0, "all GPU resources should be released");
    ctx.reset_peak();

    // Concurrent snapshots and limit/peak updates contend with creation/destruction.
    std::atomic<bool> consistent{true};
    std::thread reader([&] {
        for (int i = 0; i < 100; ++i) {
            const auto usage = ctx.memory();
            if (usage.total != usage.images + usage.masks + usage.transfers + usage.internal +
                                   usage.presentation + usage.workspace ||
                usage.peak < usage.total) {
                consistent = false;
            }
            ctx.reset_peak();
            ctx.set_memory_limit(0);
        }
    });
    for (int i = 0; i < 100; ++i) {
        auto temporary = ctx.create_image({1, 1});
        ctx.destroy(temporary);
    }
    reader.join();
    test::check(consistent && ctx.memory().total == 0, "concurrent accounting is inconsistent");
}
} // namespace

int main() {
    using namespace wgpupixel;
    test::run("mask bounds copies only the final 16-byte result of each query", [] {
        auto ctx = Context::create();
        auto selected = ctx.create_mask_bounds_buffer();
        auto empty = ctx.create_mask_bounds_buffer();
        auto cmd = ctx.create_commands();
        // One dispatch, exact and partial dispatch limits, and multiple internal batches.
        for (auto size : {ImageSize{1, 1}, {1024, 1024}, {1024, 1025}, {4096, 4096}, {6000, 4000}}) {
            auto mask = ctx.create_mask(size);
            const Rect corner{int(size.width - 1), int(size.height - 1), 1, 1};
            cmd.fill(mask, {.coverage = 1, .region = corner});
            cmd.mask_bounds(mask, selected);
            cmd.fill(mask, {.coverage = 0});
            cmd.mask_bounds(mask, empty);
            auto calls = copy_calls, bytes = copied_bytes;
            ctx.submit_and_wait(cmd);
            test::check(copy_calls - calls == 2 && copied_bytes - bytes == 32,
                        "two queries must copy exactly two 16-byte results");
            const auto bounds = ctx.read(selected);
            test::check(bounds && bounds->x == corner.x && bounds->y == corner.y &&
                            bounds->width == 1 && bounds->height == 1,
                        "readback must include the hit in the final dispatch");
            test::check(!ctx.read(empty), "the second query must observe the cleared mask");
            cmd.mask_bounds(mask, selected, {.region = Rect{0, 0, 0, 0}});
            calls = copy_calls;
            bytes = copied_bytes;
            ctx.submit_and_wait(cmd);
            test::check(copy_calls - calls == 1 && copied_bytes - bytes == 16 && !ctx.read(selected),
                        "an empty region must copy one empty result, replacing the previous one");
        }
    });
    test::run("late encoding failures discard the already submitted recording", [] {
        auto ctx = Context::create();
        for (const unsigned failure : {1u, 17u}) {
            auto image = ctx.create_image({17, 9});
            auto destination = ctx.create_image(image.size());
            auto stroke_image = ctx.create_image(image.size());
            auto stroke = ctx.create_brush_stroke_state(stroke_image);
            const std::array<StrokeSample, 1> samples{{{{8, 4}}}};
            ctx.run_and_wait([&](Commands& cmd) { cmd.fill(image, {.color = {0, 0, 0, 1}}); });
            auto cmd = ctx.create_commands();
            cmd.brush_stroke(stroke_image, stroke,
                             {.samples = samples, .brush = {.diameter = 2}, .color = {1, 0, 0, 1}});
            cmd.brightness(image, {.amount = 0.25f});
            // Each round morphology pass starts another bounded internal batch.
            for (int i = 0; i < 17; ++i)
                cmd.minimum(image, destination, {.radius = 0, .shape = MorphologyShape::round});
            const auto revision = image.revision();
            fail_encoder_after = failure;
            test::error(ErrorCode::execution_failed, "submit", "", [&] { (void)ctx.submit(cmd); });
            test::check(image.revision() == revision + (failure == 17),
                        "only a late failure may have submitted the brightness pass");
            if (failure == 17) {
                ctx.destroy(destination); // No discarded record keeps it busy.
                test::error(ErrorCode::invalid_resource, "brush_stroke", "state", [&] {
                    cmd.brush_stroke(stroke_image, stroke,
                                     {.samples = samples, .brush = {.diameter = 2},
                                      .color = {1, 0, 0, 1}});
                });
            }
            ctx.submit_and_wait(cmd);
            test::check(image.revision() == revision + 1,
                        "retry must preserve early work or leave a late-failed recorder empty");
            const auto pixels = test::read_float(ctx, image);
            for (std::size_t i = 0; i < pixels.size(); i += 4) {
                test::check(pixels[i] == 0.25f && pixels[i + 1] == 0.25f &&
                                pixels[i + 2] == 0.25f && pixels[i + 3] == 1,
                            "retry applied the submitted prefix twice");
            }
        }
    });
    test::run("failed completion invalidates stroke continuation until reset", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({17, 9});
        auto state = ctx.create_brush_stroke_state(image);
        const std::array<StrokeSample, 1> samples{{{{8, 4}}}};
        auto cmd = ctx.create_commands();
        const BrushStrokeOptions options{.samples = samples, .brush = {.diameter = 2},
                                         .color = {1, 0, 0, 1}};
        cmd.brush_stroke(image, state, options);
        fail_completion = true;
        test::error(ErrorCode::execution_failed, "wait", "",
                    [&] { ctx.submit_and_wait(cmd); });
        test::error(ErrorCode::invalid_resource, "brush_stroke", "state",
                    [&] { cmd.brush_stroke(image, state, options); });
        state.reset();
        cmd.brush_stroke(image, state, options);
        ctx.submit_and_wait(cmd);
    });
    test::run("submissions compile only the pipelines they use", [] {
        auto ctx = Context::create();
        test::check(report().computePipelines.numAllocated == 0,
                    "a new context must not compile processing pipelines");
        auto image = ctx.create_image({2, 2});
        auto cmd = ctx.create_commands();
        cmd.fill(image, {.color = {1, 0, 0, 1}});
        fail_pipeline_after = 1;
        bool failed = false;
        try {
            (void)ctx.submit(cmd);
        } catch (const Error& error) {
            failed = error.code() == ErrorCode::execution_failed;
        }
        test::check(failed && image.revision() == 0,
                    "a failed compilation must reject the submission before GPU work");
        ctx.submit_and_wait(cmd); // The rejected recording remains intact.
        test::check(image.revision() == 1 && report().computePipelines.numKeptFromUser == 1,
                    "the first fill compiles exactly its own pipeline");
        ctx.submit_and_wait(cmd);
        test::check(report().computePipelines.numKeptFromUser == 1, "compiled pipelines are reused");
        ctx.destroy(image);
    });
    test::run("native allocations stay bounded and return to baseline", [] {
        {
            auto ctx = Context::create();
            test::check(report().computePipelines.numAllocated == 0,
                        "deferred context must not compile processing pipelines");
            fail_pipeline_after = 3;
            bool failed = false;
            try {
                ctx.prepare();
            } catch (const Error& error) {
                failed = error.code() == ErrorCode::execution_failed;
            }
            test::check(failed, "injected pipeline failure must be reported");
            test::check(report().computePipelines.numKeptFromUser == 2,
                        "failed preparation must retain only completed pipelines");
            ctx.prepare();
            {
                auto source = ctx.create_image({1025, 1025});
                auto destination = ctx.create_image({1025, 1025});
                auto cmd = ctx.create_commands();
                cmd.median(source, destination, {.radius = 2});
                const auto recorded = report();
                fail_completion = true;
                test::error(ErrorCode::execution_failed, "submit", "",
                            [&] { (void)ctx.submit(cmd); });
                same_live_resources(report(), recorded);
                test::check(destination.revision() == 16,
                            "completed early batches must invalidate cached image results");
                ctx.submit_and_wait(cmd);
                test::check(destination.revision() == 16,
                            "a failed internal submission must discard its recording");
                cmd.median(source, destination, {.radius = 2});
                ctx.submit_and_wait(cmd);
                // 1025-square output splits into a 5 by 5 grid of 256-pixel tiles.
                test::check(destination.revision() == 16 + 25,
                            "each submitted write must advance the revision once");
                cmd = {};
                ctx.destroy(source);
                ctx.destroy(destination);
                ctx.reset_peak();
            }
            accounting(ctx);
            const auto baseline = report();
            ctx.prepare();
            same_live_resources(report(), baseline);
            ctx.reset_peak();
            const auto before_failure = ctx.memory();
            fail_buffer = true;
            test::error(ErrorCode::out_of_memory, "create_image", "",
                        [&] { (void)ctx.create_image({8, 8}); });
            test::check(ctx.memory().total == before_failure.total &&
                            ctx.memory().peak == before_failure.peak,
                        "driver allocation failure must roll back its ledger reservation");
            fail_buffer = true;
            test::error(ErrorCode::out_of_memory, "create_mask", "",
                        [&] { (void)ctx.create_mask({17, 9}); });
            fail_buffer = true;
            test::error(ErrorCode::out_of_memory, "create_commands", "",
                        [&] { (void)ctx.create_commands(3); });
            same_live_resources(report(), baseline);
            {
                const auto before_mask = allocated_bytes;
                auto mask = ctx.create_mask({17, 9});
                test::check(allocated_bytes - before_mask == 156,
                            "153 mask pixels must occupy one rounded 156-byte buffer");
                const auto mask_only = report();
                fail_buffer_after = 2;
                test::error(ErrorCode::out_of_memory, "create_readback_buffer", "",
                            [&] { (void)ctx.create_readback_buffer(mask); });
                same_live_resources(report(), mask_only);
                const auto before_upload = allocated_bytes;
                auto upload = ctx.create_upload_buffer(mask);
                test::check(allocated_bytes - before_upload == 156,
                            "mask upload must occupy one rounded 156-byte buffer");
                const auto before_readback = allocated_bytes;
                auto readback = ctx.create_readback_buffer(mask);
                test::check(allocated_bytes - before_readback == 312,
                            "mask readback must occupy two rounded 156-byte buffers");
                auto image = ctx.create_image({17, 9});
                auto cmd = ctx.create_commands(4);
                const auto reserved = report();
                const auto bytes = allocated_bytes;
                std::array<std::uint8_t, 17 * 9> coverage{}, result{};
                for (int frame = 0; frame < 200; ++frame) {
                    for (std::size_t i = 0; i < coverage.size(); ++i) {
                        coverage[i] = static_cast<std::uint8_t>((i + frame) % 256);
                    }
                    ctx.write(upload, coverage);
                    cmd.upload(upload, mask);
                    cmd.fill(image, {.color = {0.2f, 0.1f, 0, 0.5f}, .mask = &mask});
                    cmd.brightness(image, {.amount = 0.1f, .mask = &mask});
                    cmd.download(mask, readback);
                    ctx.submit_and_wait(cmd);
                    ctx.read(readback, result);
                    test::check(result == coverage,
                                "compact mask transfer reuse corrupted coverage");
                    test::check(allocated_bytes == bytes,
                                "masked execution allocated an additional GPU buffer");
                    same_live_resources(report(), reserved);
                }
                ctx.destroy(upload);
                ctx.destroy(readback);
                ctx.destroy(mask);
                ctx.destroy(image);
            }
            same_live_resources(report(), baseline);
            {
                auto image = ctx.create_image({17, 9});
                const auto image_only = report();
                fail_buffer_after = 2;
                test::error(ErrorCode::out_of_memory, "create_readback_buffer", "",
                            [&] { (void)ctx.create_readback_buffer(image); });
                same_live_resources(report(), image_only);
                const auto before = allocated_bytes;
                auto upload = ctx.create_upload_buffer(image);
                auto readback = ctx.create_readback_buffer(image);
                test::check(allocated_bytes - before == 17 * 9 * 4 * 3,
                            "upload needs 4 bytes/pixel; readback needs two 4-byte buffers");
                auto cmd = ctx.create_commands(2);
                const auto reserved = report();
                const auto bytes = allocated_bytes;
                std::array<std::uint8_t, 17 * 9 * 4> pixels{}, result{};
                pixels.fill(255);
                for (int frame = 0; frame < 200; ++frame) {
                    ctx.write(upload, pixels);
                    cmd.upload(upload, image);
                    cmd.download(image, readback);
                    ctx.submit_and_wait(cmd);
                    ctx.read(readback, result);
                    test::check(result == pixels, "transfer reuse corrupted pixels");
                    test::check(allocated_bytes == bytes,
                                "transfer allocated another pixel buffer");
                    same_live_resources(report(), reserved);
                }
                ctx.destroy(upload);
                ctx.destroy(readback);
                ctx.destroy(image);
            }
            same_live_resources(report(), baseline);
            {
                auto image = ctx.create_image({17, 9});
                auto blur_options = test::reserve_workspace(
                    ctx, image, GaussianBlurOptions{.radius = 2, .sigma = 1.0f},
                    gaussian_blur_requirements);
                auto cmd = ctx.create_commands(3);
                const auto reserved = report();
                test::check(reserved.buffers.numKeptFromUser ==
                                baseline.buffers.numKeptFromUser + 3,
                            "expected image, workspace and parameter buffer");
                cmd.fill(image, {.color = {0, 0, 0, 1}});
                fail_encoder = true;
                test::error(ErrorCode::execution_failed, "submit", "",
                            [&] { (void)ctx.submit(cmd); });
                same_live_resources(report(), reserved);
                // Failed encoding preserves the recording for a successful retry.
                ctx.submit_and_wait(cmd);
                // Gaussian weights allocate one reusable kernel-data buffer on first submit.
                cmd.gaussian_blur(image, blur_options);
                ctx.submit_and_wait(cmd);
                const auto with_weights = report();
                const auto weight_bytes = allocated_bytes;
                test::check(with_weights.buffers.numKeptFromUser ==
                                reserved.buffers.numKeptFromUser + 1,
                            "Gaussian weights need one reusable storage buffer");
                Submission previous;
                for (int frame = 0; frame < 1000; ++frame) {
                    cmd.fill(image, {.color = {0.2f, 0.4f, 0.6f, 1.0f}});
                    cmd.gaussian_blur(image, blur_options);
                    auto done = ctx.submit(cmd);
                    ctx.wait(done);
                    if (frame > 0) {
                        ctx.wait(previous);
                    }
                    previous = done;
                    test::check(allocated_bytes == weight_bytes, "Gaussian weights reallocated storage");
                    same_live_resources(report(), with_weights);
                }
                ctx.destroy(image);
                ctx.destroy(blur_options.workspace);
            }
            same_live_resources(report(), baseline);
            {
                auto image = ctx.create_image({32, 16});
                auto cmd = ctx.create_commands(1);
                const auto reserved = report();
                std::vector<StrokeSample> points(1000, StrokeSample{{8, 8}});
                cmd.brush_stroke(image, {.samples = std::span(points).first(1),
                                         .brush = {.diameter = 4, .spacing = 0},
                                         .color = {1, 0, 0, 1}});
                ctx.set_memory_limit(ctx.memory().total);
                test::error(ErrorCode::capacity, "brush_stroke", "memory_limit", [&] {
                    cmd.brush_stroke(image, {.samples = points,
                                             .brush = {.diameter = 4, .spacing = 0},
                                             .color = {1, 0, 0, 1}});
                });
                ctx.set_memory_limit(0);
                same_live_resources(report(), reserved);
                fail_buffer = true;
                test::error(ErrorCode::out_of_memory, "submit", "",
                            [&] { (void)ctx.submit(cmd); });
                same_live_resources(report(), reserved);
                // Words staged by the rejected stroke were discarded: one dab plus its tile table,
                // 44 bytes.
                auto bytes = allocated_bytes;
                ctx.submit_and_wait(cmd);
                test::check(allocated_bytes - bytes == 44,
                            "kernel data must be sized to its words");
                test::check(report().buffers.numKeptFromUser ==
                                reserved.buffers.numKeptFromUser + 1,
                            "kernel data must use one storage buffer per commands");
                cmd.brush_stroke(image, {.samples = points,
                                         .brush = {.diameter = 4, .spacing = 0},
                                         .color = {1, 0, 0, 1}});
                bytes = allocated_bytes;
                ctx.submit_and_wait(cmd);
                test::check(allocated_bytes - bytes == 36008, "larger data must regrow storage");
                const auto grown = report();
                bytes = allocated_bytes;
                for (int frame = 0; frame < 200; ++frame) {
                    cmd.brush_stroke(image,
                                     {.samples = std::span(points).first(frame * 5 % 1000 + 1),
                                      .brush = {.diameter = 4, .spacing = 0},
                                      .color = {0, 1, 0, 1}});
                    ctx.submit_and_wait(cmd);
                    test::check(allocated_bytes == bytes, "kernel data reuse allocated a buffer");
                    same_live_resources(report(), grown);
                }
                ctx.destroy(image);
            }
            same_live_resources(report(), baseline);
            for (int iteration = 0; iteration < 100; ++iteration) {
                auto image = ctx.create_image({8, 8});
                auto cmd = ctx.create_commands(1);
                cmd.fill(image, {.color = {1.0f, 0.0f, 0.0f, 1.0f}});
                ctx.submit_and_wait(cmd);
                ctx.destroy(image);
            }
            same_live_resources(report(), baseline);
            {
                auto image = ctx.create_image({1, 1});
                auto readback = ctx.create_readback_buffer(image);
                auto commands = ctx.create_commands(2);
                commands.fill(image, {.color = {1, 0, 0, 1}});
                commands.download(image, readback);
                fail_completion = true;
                const auto failed = ctx.submit(commands);
                const auto check_failure = [&] {
                    try {
                        ctx.wait(failed);
                        test::check(false, "injected completion failure must be reported");
                    } catch (const Error& error) {
                        test::check(error.code() == ErrorCode::execution_failed &&
                                        std::string_view(error.what()) ==
                                            "submission failed; output is not valid",
                                    "an old failure must not borrow a later unrelated diagnostic");
                    }
                };
                check_failure();
                test::error(ErrorCode::execution_failed, "is_complete", "",
                            [&] { (void)ctx.is_complete(failed); });
                std::array<std::uint8_t, 4> pixels{};
                test::error(ErrorCode::invalid_argument, "read", "pixels",
                            [&] { ctx.read(readback, pixels); });
                WGPUBufferDescriptor invalid = WGPU_BUFFER_DESCRIPTOR_INIT;
                invalid.label = {"unrelated-later-failure", WGPU_STRLEN};
                invalid.size = 16;
                const auto buffer =
                    wgpuDeviceCreateBuffer(webgpu::native_context(ctx).device, &invalid);
                if (buffer) {
                    wgpuBufferRelease(buffer);
                }
                check_failure();
                ctx.destroy(readback);
                ctx.destroy(image);
            }
            same_live_resources(report(), baseline);
        }
        same_live_resources(report(), WGPUHubReport{});
    });
    if (observed) {
        wgpuInstanceRelease(observed);
    }
    return test::finish();
}
