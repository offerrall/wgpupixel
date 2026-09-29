#include "test.h"
#include <webgpu/wgpu.h>
#include <string_view>
#include <utility>

namespace {
WGPUInstance observed = nullptr;
std::uint64_t scratch_allocations = 0;
bool fail_growth = false;
} // namespace
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
    const auto label = std::string_view(descriptor->label.data,
                                        descriptor->label.length == WGPU_STRLEN
                                            ? std::char_traits<char>::length(descriptor->label.data)
                                            : descriptor->label.length);
    // Scratch images are readable storage; staged kernel data is CopyDst-only.
    if ((descriptor->usage & WGPUBufferUsage_Storage) &&
        (descriptor->usage & WGPUBufferUsage_CopySrc)) {
        ++scratch_allocations;
    }
    if (label == "median" && std::exchange(fail_growth, false)) {
        return nullptr;
    }
    return __real_wgpuDeviceCreateBuffer(device, descriptor);
}
int main() {
    using namespace wgpupixel;
    test::run("explicit workspace is reused by run_and_wait and released with Context", [] {
        {
            auto ctx = Context::create();
            auto source = ctx.create_image({257, 129}), destination = ctx.create_image({257, 129}),
                 scratch = ctx.create_image({257, 129}), blurred = ctx.create_image({257, 129});
            ctx.run_and_wait(
                [&](Commands& cmd) { cmd.fill(source, {.color = {.3f, .2f, .1f, 1}}); });
            WorkspacePlan plan;
            plan.merge(motion_blur_requirements(source.size(), {.distance = 2000}).workspace);
            plan.merge(radial_blur_requirements(source.size(), {.amount = 100}).workspace);
            plan.merge(surface_blur_requirements(source.size(), {.radius = 100}).workspace);
            plan.merge(gaussian_blur_requirements(source.size(), {.radius = 1000, .sigma = 333})
                           .workspace);
            plan.merge(unsharp_mask_requirements(source.size(), {.radius = 1000}).workspace);
            plan.merge(high_pass_requirements(source.size(), {.radius = 250}).workspace);
            plan.merge(maximum_requirements(source.size(),
                                            {.radius = 500, .shape = MorphologyShape::round})
                           .workspace);
            plan.merge(median_requirements(source.size(), {.radius = 5}).workspace);
            auto workspace = ctx.create_workspace(plan);
            const auto filter = [&](int effect) {
                ctx.run_and_wait([&](Commands& cmd) {
                    switch (effect) {
                    case 0:
                        cmd.motion_blur(source, destination,
                                        {.distance = 2000, .workspace = workspace});
                        break;
                    case 1:
                        cmd.radial_blur(source, destination,
                                        {.amount = 100, .workspace = workspace});
                        break;
                    case 2:
                        cmd.surface_blur(source, destination,
                                         {.radius = 100, .workspace = workspace});
                        break;
                    case 3:
                        cmd.gaussian_blur(destination,
                                          {.radius = 1000, .sigma = 333, .workspace = workspace});
                        break;
                    case 4:
                        cmd.unsharp_mask(source, destination,
                                         {.radius = 1000, .workspace = workspace});
                        break;
                    case 5:
                        cmd.high_pass(source, destination, {.radius = 250, .workspace = workspace});
                        break;
                    case 6:
                        cmd.maximum(source, destination,
                                    {.radius = 500,
                                     .shape = MorphologyShape::round,
                                     .workspace = workspace});
                        break;
                    case 7:
                        cmd.median(source, destination, {.radius = 5, .workspace = workspace});
                        break;
                    }
                });
            };
            for (int effect = 0; effect < 8; ++effect) {
                filter(effect);
            }
            const auto warmed = scratch_allocations;
            test::check(warmed > 0, "fixture must allocate filter temporaries");
            for (int frame = 0; frame < 3; ++frame) {
                for (int effect = 0; effect < 8; ++effect) {
                    filter(effect);
                }
                test::check(scratch_allocations == warmed,
                            "repeated previews allocated fresh scratch buffers");
            }
        }
        WGPUGlobalReport report{};
        wgpuGenerateReport(observed, &report);
        test::check(report.hub.buffers.numKeptFromUser == 0 && report.hub.buffers.numAllocated == 0,
                    "Context teardown leaked workspace buffers");
        wgpuInstanceRelease(observed);
        observed = nullptr;
    });
    test::run("automatic command growth preserves the recorder on allocation failure", [] {
        {
            auto ctx = Context::create();
            auto source = ctx.create_image({513, 521}), destination = ctx.create_image({513, 521});
            auto cmd = ctx.create_commands(1);
            fail_growth = true;
            test::error(ErrorCode::out_of_memory, "median", "",
                        [&] { cmd.median(source, destination, {.radius = 2}); });
            cmd.fill(destination, {.color = {.3f, .2f, .1f, 1}});
            ctx.submit_and_wait(cmd);
            cmd.median(source, destination, {.radius = 2});
            ctx.submit_and_wait(cmd);
        }
        wgpuInstanceRelease(observed);
        observed = nullptr;
    });
    return test::finish();
}
