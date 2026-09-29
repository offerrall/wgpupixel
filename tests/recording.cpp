#include "test.h"
#include "../src/utils/commands.h"
#include <wgpupixel_webgpu.h>
#include <array>
#include <chrono>
#include <thread>

using namespace wgpupixel;
namespace {
bool restrict_data = false;
unsigned fail_allocation = 0;
}

extern "C" WGPUStatus __real_wgpuDeviceGetLimits(WGPUDevice, WGPULimits*);
extern "C" WGPUStatus __wrap_wgpuDeviceGetLimits(WGPUDevice device, WGPULimits* limits) {
    const auto status = __real_wgpuDeviceGetLimits(device, limits);
    if (restrict_data && status == WGPUStatus_Success) {
        // Four aligned payloads fit. A wide smudge with five dabs must fail late.
        limits->maxBufferSize = 1024 * 1024;
        limits->maxStorageBufferBindingSize = 1024 * 1024;
        limits->minStorageBufferOffsetAlignment = 256 * 1024;
    }
    return status;
}
extern "C" WGPUBuffer __real_wgpuDeviceCreateBuffer(WGPUDevice, const WGPUBufferDescriptor*);
extern "C" WGPUBuffer __wrap_wgpuDeviceCreateBuffer(WGPUDevice device,
                                                    const WGPUBufferDescriptor* descriptor) {
    if (fail_allocation && --fail_allocation == 0) return nullptr;
    return __real_wgpuDeviceCreateBuffer(device, descriptor);
}

namespace {
template <class T> std::span<std::uint8_t> bytes(std::vector<T>& values) {
    return {reinterpret_cast<std::uint8_t*>(values.data()), values.size() * sizeof(T)};
}
void await_poll(Context& ctx, const Submission& submission) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!ctx.is_complete(submission)) {
        test::check(std::chrono::steady_clock::now() < deadline, "completion polling timed out");
        std::this_thread::yield();
    }
}
}

int main() {
    test::run("recording checkpoint restores appended resources, data and dissolve sequence", [] {
        // Exercise unwinding from a non-library exception, after multiple appends.
        detail::Recording recording;
        recording.owner = std::make_shared<detail::State>();
        recording.owner->limits.maxBufferSize = 1024;
        recording.owner->limits.maxStorageBufferBindingSize = 1024;
        recording.owner->limits.minStorageBufferOffsetAlignment = 4;
        recording.capacity = 8;
        recording.stride = 1;
        recording.records.reserve(8);
        recording.parameters.resize(8);
        auto destination = std::make_shared<detail::Resource>();
        destination->width = destination->height = 1;
        {
            detail::Operation op(&recording, "fill");
            op.append({detail::kernel_record(detail::Kernel::fill, destination)});
        }
        recording.next_dissolve_index = 7;
        std::weak_ptr<detail::Resource> temporary;
        try {
            detail::Operation op(&recording, "smudge_stroke");
            auto scratch = std::make_shared<detail::Resource>();
            scratch->width = scratch->height = 1;
            temporary = scratch;
            auto record = detail::kernel_record(detail::Kernel::smudge, scratch, destination);
            op.data(record, 8, "samples");
            op.append({record});
            record.data_offset = detail::no_data;
            op.data(record, 8, "samples");
            op.append({record});
            ++recording.next_dissolve_index;
            throw std::runtime_error("injected late failure");
        } catch (const std::runtime_error&) {
        }
        test::check(recording.records.size() == 1 && recording.data.empty() &&
                        recording.next_dissolve_index == 7 && destination->recorded == 1 &&
                        temporary.expired(), "checkpoint must restore the entire recording");
    });

    test::run("smudge late data failure restores prior data, references and grown uniforms", [] {
        restrict_data = true;
        auto ctx = Context::create();
        restrict_data = false;
        auto image = ctx.create_image({17, 9});
        const Brush brush{.diameter = 130, .spacing = .01f};
        auto scratch =
            ctx.create_workspace(smudge_stroke_requirements({17, 9}, {.brush = brush}).workspace);
        auto cmd = ctx.create_commands(1);
        const std::array points{StrokeSample{{8, 4}}};
        cmd.brush_stroke(image, {.samples = points,
                                 .brush = {.diameter = 200, .hardness = 1},
                                 .color = {1, 0, 0, 1}});
        const auto before = ctx.memory();
        const std::array samples{StrokeSample{{2, 4}}, StrokeSample{{15, 4}}};
        test::error(ErrorCode::capacity, "smudge_stroke", "samples", [&] {
            cmd.smudge_stroke(image, {.samples = samples, .brush = brush, .workspace = scratch});
        });
        test::check(ctx.memory().total == before.total && image.revision() == 0,
                    "failed operation must restore uniform storage and preserve revision");
        ctx.destroy(scratch); // No reference from a partially recorded dab may survive.
        ctx.submit_and_wait(cmd);
        const auto actual = test::read(ctx, image);
        for (std::size_t i = 0; i < actual.size(); ++i)
            test::check(actual[i] == std::array<std::uint8_t, 4>{255, 0, 0, 255}[i % 4],
                        "earlier brush payload or records were corrupted");
    });

    test::run("median command growth failure preserves caller workspace and permits retry", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({33, 17});
        auto destination = ctx.create_image({33, 17});
        auto cmd = ctx.create_commands(1);
        cmd.fill(source, {.color = {-.5f, 2, 0, 1}});
        auto workspace = ctx.create_workspace(median_requirements(source.size(), {.radius = 5}).workspace);
        const auto before = ctx.memory();
        // Workspace is already reserved. Reject the recorder's uniform buffer growth.
        fail_allocation = 1;
        test::error(ErrorCode::out_of_memory, "median", "", [&] {
            cmd.median(source, destination, {.radius = 5, .workspace = workspace});
        });
        test::check(ctx.memory().total == before.total, "failed filter retained GPU allocations");
        cmd.median(source, destination, {.radius = 5, .workspace = workspace});
        ctx.submit_and_wait(cmd);
        ctx.destroy(workspace);
        ctx.destroy(destination);
        ctx.submit_and_wait(cmd);
        cmd.fill(source, {.color = {0, 1, 0, 1}});
        ctx.submit_and_wait(cmd);
    });

    test::run("requirements reserve all filter workspace before recording", [] {
        auto ctx = Context::create();
        constexpr ImageSize size{33, 17};
        auto source = ctx.create_image(size), destination = ctx.create_image(size);
        auto cmd = ctx.create_commands(4096);
        const auto check = [&](const WorkspacePlan& plan, auto record) {
            auto workspace = ctx.create_workspace(plan);
            const auto baseline = ctx.memory();
            for (int round = 0; round < 2; ++round) {
                record(workspace);
                test::check(ctx.memory().total == baseline.total,
                            "filter recording allocated unplanned GPU storage");
                cmd = {};
                cmd = ctx.create_commands(4096);
                test::check(ctx.memory().total == baseline.total,
                            "abandoned recording retained hidden GPU storage");
            }
        };
        check(gaussian_blur_requirements(size, {.radius = 256, .sigma = 32}).workspace,
              [&](const Workspace& workspace) {
                  cmd.gaussian_blur(source, {.radius = 256, .sigma = 32, .workspace = workspace});
              });
        check(median_requirements(size, {.radius = 5}).workspace, [&](const Workspace& workspace) {
            cmd.median(source, destination, {.radius = 5, .workspace = workspace});
        });
        check(motion_blur_requirements(size, {.distance = 100}).workspace,
              [&](const Workspace& workspace) {
                  cmd.motion_blur(source, destination, {.distance = 100, .workspace = workspace});
              });
        check(radial_blur_requirements(size, {.amount = 100}).workspace,
              [&](const Workspace& workspace) {
                  cmd.radial_blur(source, destination, {.amount = 100, .workspace = workspace});
              });
        check(surface_blur_requirements(size, {.radius = 100}).workspace,
              [&](const Workspace& workspace) {
                  cmd.surface_blur(source, destination, {.radius = 100, .workspace = workspace});
              });
        check(minimum_requirements(size, {.radius = 50}).workspace,
              [&](const Workspace& workspace) {
                  cmd.minimum(source, destination, {.radius = 50, .workspace = workspace});
              });
        check(maximum_requirements(size, {.radius = 50}).workspace,
              [&](const Workspace& workspace) {
                  cmd.maximum(source, destination, {.radius = 50, .workspace = workspace});
              });
        check(unsharp_mask_requirements(size, {.radius = 100}).workspace,
              [&](const Workspace& workspace) {
                  cmd.unsharp_mask(source, destination, {.radius = 100, .workspace = workspace});
              });
        check(high_pass_requirements(size, {.radius = 100}).workspace,
              [&](const Workspace& workspace) {
                  cmd.high_pass(source, destination, {.radius = 100, .workspace = workspace});
              });
    });

    test::run("continued smudge grows replay storage and preserves state on failure", [] {
        auto ctx = Context::create();
        auto whole = ctx.create_image({17, 9}), split = ctx.create_image({17, 9});
        const Brush brush{.diameter = 130, .hardness = 1, .spacing = .01f};
        auto scratch =
            ctx.create_workspace(smudge_stroke_requirements({17, 9}, {.brush = brush}).workspace);
        const std::array samples{StrokeSample{{2, 4}}, StrokeSample{{15, 4}}};
        const auto options = [&](std::span<const StrokeSample> events) {
            return SmudgeStrokeOptions{.samples = events, .brush = brush, .strength = .7f,
                                       .finger_painting = true, .color = {-1, 2, 0, 1}};
        };
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.fill(whole, {.color = {0, 0, 1, 1}});
            cmd.fill(split, {.color = {0, 0, 1, 1}});
            cmd.smudge_stroke(whole, test::with_workspace(options(samples), scratch));
        });
        auto state = ctx.create_smudge_stroke_state(split);
        auto cmd = ctx.create_commands(1);
        cmd.smudge_stroke(split, state,
                          test::with_workspace(options(std::span(samples).first(1)), scratch));
        ctx.submit_and_wait(cmd);
        const auto before = ctx.memory().total;
        fail_allocation = 1; // Cumulative replay must grow beyond the two initial slots.
        test::error(ErrorCode::out_of_memory, "smudge_stroke", "", [&] {
            cmd.smudge_stroke(split, state,
                              test::with_workspace(options(std::span(samples).last(1)), scratch));
        });
        test::check(ctx.memory().total == before, "failed replay retained GPU storage");
        cmd.smudge_stroke(split, state,
                          test::with_workspace(options(std::span(samples).last(1)), scratch));
        ctx.submit_and_wait(cmd);
        test::check(test::read(ctx, split) == test::read(ctx, whole),
                    "failed continuation changed the retained stroke state");
    });

    test::run("automatic growth preserves HDR, alpha, mask and region over repeated reuse", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({17, 9});
        auto mask = ctx.create_mask({17, 9});
        auto upload = ctx.create_upload_buffer(image, {.format = TransferFormat::rgba32_float});
        auto readback = ctx.create_readback_buffer(image, {.format = TransferFormat::rgba32_float});
        std::vector<float> input(17 * 9 * 4), output(input.size());
        for (std::size_t i = 0; i < input.size(); i += 4) {
            const float alpha = std::array{0.f, 0x1p-20f, .5f, 1.f}[(i / 4) % 4];
            input[i] = -alpha; input[i + 1] = 2 * alpha;
            input[i + 2] = .5f * alpha; input[i + 3] = alpha;
        }
        ctx.write(upload, bytes(input));
        auto cmd = ctx.create_commands(0);
        const auto baseline = ctx.memory();
        const auto stride = baseline.internal; // One initial command slot.
        ctx.reset_peak();
        for (int reuse = 0; reuse < 2; ++reuse) {
            cmd.upload(upload, image);
            cmd.fill(mask, {.coverage = 1});
            // An even number of linear inversions is an independent identity.
            for (int i = 0; i < 600; ++i)
                cmd.invert(image, {.mask = &mask, .region = Rect{1, 1, 15, 7}});
            cmd.download(image, readback);
            const auto allocated = ctx.memory();
            test::check(allocated.internal == 1024 * stride &&
                            allocated.peak == baseline.total - stride + 1536 * stride,
                        "growth must count old and new uniforms exactly, then reuse storage");
            const auto submitted = ctx.submit(cmd);
            await_poll(ctx, submitted);
            test::check(ctx.is_complete(submitted), "completion must remain true");
            ctx.read(readback, bytes(output));
            test::check(output == input, "command growth corrupted pixels");
        }
        const auto limits = ctx.limits();
        WGPULimits native = WGPU_LIMITS_INIT;
        __real_wgpuDeviceGetLimits(webgpu::native_context(ctx).device, &native);
        test::check(limits.max_buffer_bytes == native.maxBufferSize &&
                        limits.max_storage_binding_bytes == native.maxStorageBufferBindingSize &&
                        limits.max_image_pixels * 16 <= limits.max_storage_binding_bytes &&
                        limits.max_image_dimension == 8 * native.maxComputeWorkgroupsPerDimension &&
                        limits.staged_data_alignment == native.minStorageBufferOffsetAlignment,
                    "public limits differ from the device");
        test::error(ErrorCode::capacity, "create_image", "size", [&] {
            (void)ctx.create_image({1, std::int64_t(limits.max_image_pixels + 1)});
        });
        test::error(ErrorCode::invalid_resource, "is_complete", "submission", [&] {
            (void)ctx.is_complete({});
        });
        auto display = webgpu::Display::create(ctx, 17, 9);
        test::error(ErrorCode::invalid_resource, "display.draw", "image", [&] {
            (void)display.draw(Image{});
        });
        auto shown = display.draw(image);
        await_poll(ctx, shown);
        shown = display.draw(image, {.view = Affine::scale(.5f)});
        await_poll(ctx, shown);
        ctx.destroy(image); // Polling retires both presentation variants' references.
        display.close();
    });
    return test::finish();
}
