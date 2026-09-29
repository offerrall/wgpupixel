#include "../test.h"
#include <bit>
#include <cstring>
#include <type_traits>

#ifdef WGPUPIXEL_TEST_STROKE_COST
#include <webgpu/webgpu.h>
#include <chrono>
#include <map>

namespace {
struct Cost {
    std::uint64_t copy_bytes = 0, copy_lanes = 0, replay_groups = 0, copy_dispatches = 0;
    bool operator==(const Cost&) const = default;
};
bool count_cost = false, copy_pipeline = false;
Cost cost;
std::map<WGPUComputePipeline, bool> copy_pipelines;
}
extern "C" WGPUComputePipeline __real_wgpuDeviceCreateComputePipeline(
    WGPUDevice, const WGPUComputePipelineDescriptor*);
extern "C" WGPUComputePipeline __wrap_wgpuDeviceCreateComputePipeline(
    WGPUDevice device, const WGPUComputePipelineDescriptor* descriptor) {
    auto pipeline = __real_wgpuDeviceCreateComputePipeline(device, descriptor);
    const std::string_view label(descriptor->label.data,
        descriptor->label.length == WGPU_STRLEN ? std::strlen(descriptor->label.data)
                                                : descriptor->label.length);
    copy_pipelines[pipeline] = label == "copy_image" || label == "stroke_copy_mask";
    return pipeline;
}
extern "C" void __real_wgpuComputePassEncoderSetPipeline(WGPUComputePassEncoder, WGPUComputePipeline);
extern "C" void __wrap_wgpuComputePassEncoderSetPipeline(WGPUComputePassEncoder pass,
                                                         WGPUComputePipeline pipeline) {
    copy_pipeline = copy_pipelines.at(pipeline);
    __real_wgpuComputePassEncoderSetPipeline(pass, pipeline);
}
extern "C" void __real_wgpuComputePassEncoderDispatchWorkgroups(
    WGPUComputePassEncoder, std::uint32_t, std::uint32_t, std::uint32_t);
extern "C" void __wrap_wgpuComputePassEncoderDispatchWorkgroups(
    WGPUComputePassEncoder pass, std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    if (count_cost) {
        if (copy_pipeline) {
            cost.copy_lanes += std::uint64_t(x) * y * z * 64;
            cost.copy_dispatches += x && y && z;
        } else {
            cost.replay_groups += std::uint64_t(x) * y * z;
        }
    }
    __real_wgpuComputePassEncoderDispatchWorkgroups(pass, x, y, z);
}
extern "C" void __real_wgpuCommandEncoderCopyBufferToBuffer(
    WGPUCommandEncoder, WGPUBuffer, std::uint64_t, WGPUBuffer, std::uint64_t, std::uint64_t);
extern "C" void __wrap_wgpuCommandEncoderCopyBufferToBuffer(
    WGPUCommandEncoder encoder, WGPUBuffer source, std::uint64_t source_offset,
    WGPUBuffer destination, std::uint64_t destination_offset, std::uint64_t bytes) {
    if (count_cost) cost.copy_bytes += bytes;
    __real_wgpuCommandEncoderCopyBufferToBuffer(encoder, source, source_offset,
                                               destination, destination_offset, bytes);
}
#endif

using namespace wgpupixel;
namespace {
// Odd width/height exercise rectangles and row ends sharing packed A8 words.
constexpr ImageSize size{97, 73};
constexpr std::array samples{
    StrokeSample{{43.25f, 32.75f}, .2f, .1f, 350},
    StrokeSample{{65.5f, 17.25f}, .9f, .8f, 10},
    StrokeSample{{16.75f, 53.5f}, .55f, .5f, 90},
    StrokeSample{{-18, 81}, .8f, .2f, 200},
    StrokeSample{{118, -15}, .3f, .7f, 270},
    StrokeSample{{18, 9}, 0, 1, 0},
    StrokeSample{{54, 38}, .75f, .4f, 40}};

template <class Target> std::vector<std::uint8_t> fixture(const Target& target) {
    const auto count = std::size_t(target.size().width * target.size().height);
    if constexpr (std::is_same_v<Target, Mask>) {
        std::vector<std::uint8_t> result(count);
        for (std::size_t i = 0; i < count; ++i) result[i] = (i * 137 + i / 11) % 256;
        return result;
    } else {
        std::vector<float> floats(4 * count);
        for (std::size_t i = 0; i < count; ++i) {
            const auto alpha = std::array{0.f, .125f, .5f, 1.f}[i % 4];
            floats[4 * i] = -float(i % 13) / 13 * alpha;
            floats[4 * i + 1] = float(i % 19) / 8 * alpha;
            floats[4 * i + 2] = float(i % 7) / 7 * alpha;
            floats[4 * i + 3] = alpha;
        }
        std::vector<std::uint8_t> result(floats.size() * sizeof(float));
        std::memcpy(result.data(), floats.data(), result.size());
        return result;
    }
}

template <class Target> auto upload_buffer(Context& ctx, const Target& target) {
    if constexpr (std::is_same_v<Target, Image>)
        return ctx.create_upload_buffer(target, {.format = TransferFormat::rgba32_float});
    else return ctx.create_upload_buffer(target);
}
template <class Target> auto readback_buffer(Context& ctx, const Target& target) {
    if constexpr (std::is_same_v<Target, Image>)
        return ctx.create_readback_buffer(target, {.format = TransferFormat::rgba32_float});
    else return ctx.create_readback_buffer(target);
}

// The reference is the public single-call operation on the original pixels, not a
// second implementation of shader math. Compare storage bytes, including float bits,
// at EVERY prefix of EVERY partition of these seven samples (64 partitions).
template <class Target, class State, class Options, class Draw>
void splits(Context& ctx, const Target& target, State& state, Options options, Draw draw,
            std::span<const StrokeSample> events = samples) {
    auto upload = upload_buffer(ctx, target);
    auto readback = readback_buffer(ctx, target);
    const auto input = fixture(target);
    ctx.write(upload, input);
    auto cmd = ctx.create_commands();
    std::vector<std::vector<std::uint8_t>> expected(events.size());
    for (std::size_t n = 1; n <= events.size(); ++n) {
        cmd.upload(upload, target);
        options.samples = events.first(n);
        draw(cmd, target, options);
        cmd.download(target, readback);
        ctx.submit_and_wait(cmd);
        expected[n - 1].resize(input.size());
        ctx.read(readback, expected[n - 1]);
    }
    std::vector<std::uint8_t> actual(input.size());
    const auto memory = ctx.memory();
    for (unsigned partition = 0; partition < (1u << (events.size() - 1)); ++partition) {
        state.reset();
        test::check(!state.snapshot_bounds(), "reset retained snapshot bounds");
        cmd.upload(upload, target);
        // Empty prefixes must retain their discard guard without snapshot traffic.
        options.samples = {};
        draw(cmd, target, state, options);
        test::check(!state.snapshot_bounds(), "empty stroke has snapshot bounds");
        std::size_t first = 0;
        for (std::size_t n = 1; n <= events.size(); ++n) {
            if (n < events.size() && !(partition & (1u << (n - 1)))) continue;
            options.samples = events.subspan(first, n - first);
            draw(cmd, target, state, options);
            if (partition % 7 == 0) {
                options.samples = {};
                draw(cmd, target, state, options);
            }
            if (const auto b = state.snapshot_bounds()) {
                test::check(b->x >= 0 && b->y >= 0 && b->width > 0 && b->height > 0 &&
                            b->x + b->width <= target.size().width &&
                            b->y + b->height <= target.size().height,
                            "snapshot bounds exceed canvas");
                if (const auto r = options.region) {
                    test::check(b->x >= r->x && b->y >= r->y &&
                                b->x + b->width <= r->x + r->width &&
                                b->y + b->height <= r->y + r->height,
                                "snapshot bounds exceed selection region");
                }
            }
            cmd.download(target, readback);
            ctx.submit_and_wait(cmd);
            ctx.read(readback, actual);
            test::check(actual == expected[n - 1],
                        "continuation differs in storage bits at partition " +
                        std::to_string(partition) + ", prefix " + std::to_string(n));
            first = n;
        }
        test::check(ctx.memory().images == memory.images && ctx.memory().masks == memory.masks,
                    "continuation grew snapshot storage");
    }
}

const auto paint = [](Commands& cmd, const auto& target, auto&... args) {
    cmd.brush_stroke(target, args...);
};
const auto erase = [](Commands& cmd, const auto& target, auto&... args) {
    cmd.eraser_stroke(target, args...);
};
const auto smudge = [](Commands& cmd, const auto& target, auto&... args) {
    cmd.smudge_stroke(target, args...);
};
} // namespace

int main() {
    test::run("brush, eraser, A8 and smudge continuation match every sample partition exactly", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image(size);
        auto mask = ctx.create_mask(size), selection = ctx.create_mask(size);
        auto tip = ctx.create_mask({9, 5});
        auto selection_upload = ctx.create_upload_buffer(selection);
        auto tip_upload = ctx.create_upload_buffer(tip);
        ctx.write(selection_upload, fixture(selection));
        ctx.write(tip_upload, fixture(tip));
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.upload(selection_upload, selection);
            cmd.upload(tip_upload, tip);
        });
        auto state = ctx.create_brush_stroke_state(image);
        auto mask_state = ctx.create_brush_stroke_state(mask);
        auto smudge_state = ctx.create_smudge_stroke_state(image);
        for (int config = 0; config < 4; ++config) {
            const auto selected = config & 1 ? &selection : nullptr;
            const auto region = config & 2 ? std::optional(Rect{7, 5, 79, 57}) : std::nullopt;
            const Brush brush{.diameter = 13, .hardness = .4f, .roundness = .6f, .angle = 23,
                              .spacing = config == 3 ? 0.f : .23f,
                              .tip = config & 2 ? &tip : nullptr,
                              .minimum_size = .2f, .minimum_opacity = .25f, .minimum_flow = .3f,
                              .size_jitter = .3f, .opacity_jitter = .2f, .flow_jitter = .4f,
                              .angle_jitter = .7f, .roundness_jitter = .8f,
                              .minimum_roundness = .15f, .tilt_roundness = true,
                              .angle_control = BrushAngleControl::direction, .scatter = .7f,
                              .scatter_both_axes = true, .seed = 731};
            for (bool preserve : {false, true}) {
                splits(ctx, image, state,
                       BrushStrokeOptions{.brush = brush, .color = {-.1f, 1.3f, .2f, .7f},
                           .mode = BlendMode::multiply, .opacity = .61f, .flow = .37f,
                           .preserve_alpha = preserve, .mask = selected, .region = region}, paint);
                const auto workspace = ctx.create_workspace(
                    smudge_stroke_requirements(size, {.brush = brush}).workspace);
                splits(ctx, image, smudge_state,
                       SmudgeStrokeOptions{.brush = brush, .strength = .73f,
                           .finger_painting = preserve, .color = {-.2f, 1.2f, .3f, .65f},
                           .preserve_alpha = preserve, .mask = selected, .region = region,
                           .workspace = workspace}, smudge);
            }
            const EraserStrokeOptions eraser{.brush = brush, .opacity = .57f, .flow = .29f,
                                              .mask = selected, .region = region};
            splits(ctx, image, state, eraser, erase);
            splits(ctx, mask, mask_state, eraser, erase);
            splits(ctx, mask, mask_state,
                   MaskBrushStrokeOptions{.brush = brush, .coverage = .83f,
                       .opacity = .61f, .flow = .37f, .mask = selected, .region = region}, paint);
        }
    });
    test::run("wide smudge preserves pigment read outside a narrow write region", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({193, 159});
        auto state = ctx.create_smudge_stroke_state(image);
        const Brush brush{.diameter = 135, .hardness = .3f, .spacing = 0};
        auto workspace = ctx.create_workspace(
            smudge_stroke_requirements(image.size(), {.brush = brush}).workspace);
        const std::array events{StrokeSample{{-30, 15}}, StrokeSample{{55, 15}},
                               StrokeSample{{105, 95}}, StrokeSample{{175, 180}}};
        splits(ctx, image, state,
               SmudgeStrokeOptions{.brush = brush, .strength = .71f,
                   .preserve_alpha = true, .region = Rect{71, 31, 13, 83}, .workspace = workspace},
               smudge, events);
    });
    test::run("pressure expands all four snapshot strips in one call, including odd mask rows", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image(size);
        auto mask = ctx.create_mask(size);
        auto state = ctx.create_brush_stroke_state(image);
        auto mask_state = ctx.create_brush_stroke_state(mask);
        const Brush brush{.diameter = 41, .hardness = .5f, .spacing = 0, .minimum_size = .1f};
        const std::array events{StrokeSample{{45.5f, 35.5f}, .1f},
            StrokeSample{{45.5f, 35.5f}, .4f}, StrokeSample{{45.5f, 35.5f}, 1}};
        splits(ctx, image, state,
               BrushStrokeOptions{.brush = brush, .color = {1, .1f, 0, 1}, .opacity = .63f},
               paint, events);
        splits(ctx, mask, mask_state,
               MaskBrushStrokeOptions{.brush = brush, .coverage = .15f, .opacity = .63f},
               paint, events);
    });
    test::run("off-canvas and stationary prefixes, zero flow and empty regions", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image(size);
        auto state = ctx.create_brush_stroke_state(image);
        const std::array events{StrokeSample{{-100, -100}}, StrokeSample{{-100, -100}},
                               StrokeSample{{30, 20}}, StrokeSample{{75, 55}}};
        const Brush brush{.diameter = 7, .spacing = 0, .angle_control = BrushAngleControl::direction,
                          .scatter = 2, .scatter_both_axes = true, .seed = 99};
        splits(ctx, image, state, BrushStrokeOptions{.brush = brush, .color = {1, 0, 0, 1}},
               paint, events);
        splits(ctx, image, state, BrushStrokeOptions{.brush = brush, .flow = 0}, paint, events);
        test::check(!state.snapshot_bounds(), "zero flow captured pixels");
        splits(ctx, image, state, BrushStrokeOptions{.brush = brush, .opacity = 0}, paint, events);
        test::check(!state.snapshot_bounds(), "zero opacity captured pixels");
        splits(ctx, image, state, BrushStrokeOptions{.brush = brush, .region = Rect{3, 5, 0, 20}},
               paint, events);
        test::check(!state.snapshot_bounds(), "empty region captured pixels");
        splits(ctx, image, state, BrushStrokeOptions{.brush = brush, .region = Rect{-50, -30, 5, 9}},
               paint, events);
        test::check(!state.snapshot_bounds(), "off-canvas region captured pixels");
        auto discarded = ctx.create_commands();
        discarded.brush_stroke(image, state, {.samples = std::span(events).first(1), .brush = brush});
        discarded = {};
        test::check(!state.snapshot_bounds(), "invalid state exposes snapshot bounds");
        auto cmd = ctx.create_commands();
        test::error(ErrorCode::invalid_resource, "brush_stroke", "state", [&] {
            cmd.brush_stroke(image, state, {.samples = std::span(events).last(1), .brush = brush});
        });
        auto smudge_state = ctx.create_smudge_stroke_state(image);
        auto workspace = ctx.create_workspace(
            smudge_stroke_requirements(size, {.brush = brush}).workspace);
        for (const auto region : {Rect{-50, -30, 5, 9}, Rect{3, 5, 0, 20}}) {
            splits(ctx, image, smudge_state,
                   SmudgeStrokeOptions{.brush = brush, .region = region, .workspace = workspace},
                   smudge, events);
            test::check(!smudge_state.snapshot_bounds(), "empty smudge region captured pixels");
        }
    });
#ifdef WGPUPIXEL_TEST_STROKE_COST
    test::run("snapshot GPU work scales with bounds, identically at 512 squared and 24 MP", [] {
        auto ctx = Context::create();
        ctx.prepare();
        // Smaller software adapters still verify canvas independence deterministically.
        const ImageSize larger = ctx.limits().max_image_pixels >= 24000000
                                     ? ImageSize{6000, 4000} : ImageSize{1024, 1024};
        // Force first-dab heading changes, growth in all directions and unchanged bounds.
        const std::array events{StrokeSample{{200, 200}}, StrokeSample{{220, 170}},
            StrokeSample{{160, 230}}, StrokeSample{{250, 140}}, StrokeSample{{150, 240}},
            StrokeSample{{180, 190}}, StrokeSample{{180, 190}}};
        for (int tool = 0; tool < 4; ++tool) {
            std::vector<Cost> small;
            for (const auto canvas : {ImageSize{512, 512}, larger}) {
                Image image;
                Mask mask;
                BrushStrokeState brush_state;
                SmudgeStrokeState smudge_state;
                const Brush brush{.diameter = 19, .hardness = .6f, .spacing = .2f,
                                  .scatter = .3f, .seed = 7};
                Workspace workspace;
                if (tool == 2) {
                    mask = ctx.create_mask(canvas);
                    brush_state = ctx.create_brush_stroke_state(mask);
                } else {
                    image = ctx.create_image(canvas);
                    if (tool == 3) {
                        smudge_state = ctx.create_smudge_stroke_state(image);
                        workspace = ctx.create_workspace(
                            smudge_stroke_requirements(canvas, {.brush = brush}).workspace);
                    } else brush_state = ctx.create_brush_stroke_state(image);
                }
                const auto reserved = ctx.memory();
                test::check(tool == 2 ? reserved.masks == 2 * canvas.width * canvas.height
                                     : reserved.images == 2 * canvas.width * canvas.height * 16,
                            "canvas snapshot reservation/accounting changed");
                auto cmd = ctx.create_commands();
                double elapsed_ms = 0;
                for (std::size_t i = 0; i < events.size(); ++i) {
                    const auto part = std::span(events).subspan(i, 1);
                    cost = {};
                    count_cost = true;
                    const auto start = std::chrono::steady_clock::now();
                    if (tool == 0) cmd.brush_stroke(image, brush_state,
                        {.samples = part, .brush = brush, .color = {1, 0, 0, 1}, .opacity = .6f});
                    if (tool == 1) cmd.eraser_stroke(image, brush_state,
                        {.samples = part, .brush = brush, .opacity = .6f});
                    if (tool == 2) cmd.brush_stroke(mask, brush_state,
                        {.samples = part, .brush = brush, .opacity = .6f});
                    if (tool == 3) cmd.smudge_stroke(image, smudge_state,
                        {.samples = part, .brush = brush, .workspace = workspace});
                    const auto bounds = tool == 3 ? smudge_state.snapshot_bounds()
                                                  : brush_state.snapshot_bounds();
                    ctx.submit_and_wait(cmd);
                    elapsed_ms += std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start).count();
                    count_cost = false;
                    const auto area = bounds ? std::uint64_t(bounds->width) * bounds->height : 0;
                    // Up to five disjoint rectangles (new strips + previous bounds),
                    // each rounded up by at most 7 pixels per dimension for 8x8 groups.
                    const auto padding = bounds ? 35 * (bounds->width + bounds->height) + 245 : 0;
                    test::check(cost.copy_bytes == 0 && cost.copy_dispatches <= 5 &&
                                cost.copy_lanes <= area + padding,
                                "snapshot traffic exceeded accumulated stroke bounds");
                    test::check(ctx.memory().images == reserved.images &&
                                ctx.memory().masks == reserved.masks,
                                "stroke allocated snapshot storage during gesture");
                    if (canvas.width == 512) small.push_back(cost);
                    else test::check(cost == small[i], "GPU work grew with canvas dimensions");
                }
                if (canvas == larger) {
                    std::cout << canvas.width << 'x' << canvas.height << " tool " << tool
                              << ": " << elapsed_ms / events.size()
                              << " ms/call, final copy dispatch upper bound " << cost.copy_lanes
                              << " pixels (" << cost.copy_lanes * (tool == 2 ? 1 : 16)
                              << " logical bytes)\n";
                }
            }
        }
    });
#endif
    return test::finish();
}
