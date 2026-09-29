#include "painting_reference.h"
#include <cstdlib>
#include <new>
#include <atomic>
#include <wgpupixel_webgpu.h>

namespace {
std::atomic<std::size_t> reject_size{0};
std::atomic<bool> rejected{false};
} // namespace
void* operator new(std::size_t bytes) {
    if (bytes == reject_size.load() && bytes != 0) {
        rejected = true;
        throw std::bad_alloc();
    }
    if (void* p = std::malloc(std::max(bytes, std::size_t{1}))) {
        return p;
    }
    throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept {
    std::free(pointer);
}
void operator delete(void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

using namespace wgpupixel;
int main() {
    test::run("smudge discards every record after a later data allocation failure", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({19, 13});
        const Brush brush{.diameter = 1, .spacing = 0};
        const auto plan = smudge_stroke_requirements({1, 1}, {.brush = brush}).workspace;
        auto scratch = ctx.create_workspace(plan);
        std::vector<StrokeSample> samples(200000, StrokeSample{{9.5f, 6.5f}});
        WGPULimits limits = WGPU_LIMITS_INIT;
        wgpuDeviceGetLimits(webgpu::native_context(ctx).device, &limits);
        // Inject at the second chunk's data growth, after all dab placement allocations.
        const std::size_t chunk = (1u << 22) / ((plan.bytes() / 16));
        const std::size_t words = 8 * chunk;
        const std::size_t alignment = std::max(1u, limits.minStorageBufferOffsetAlignment / 4);
        const auto second_end = (words + alignment - 1) / alignment * alignment + words;
        auto cmd = ctx.create_commands(8);
        cmd.fill(image, {.color = {0, 1, 0, 1}});
        reject_size = 4 * std::max(2 * words, second_end);
        try {
            test::error(ErrorCode::out_of_memory, "smudge_stroke", "samples", [&] {
                cmd.smudge_stroke(image, {.samples = samples,
                                          .brush = brush,
                                          .strength = 1,
                                          .finger_painting = true,
                                          .color = {1, 0, 0, 1},
                                          .workspace = scratch});
            });
        } catch (...) {
            reject_size = 0;
            throw;
        }
        reject_size = 0;
        test::check(rejected, "allocation injection did not reach the second data chunk");
        ctx.submit_and_wait(cmd);
        for (const auto& pixel : reference::download(ctx, image).pixels) {
            test::near(pixel[0], 0, 0);
            test::near(pixel[1], 1, 0);
            test::near(pixel[3], 1, 0);
        }
    });
    return test::finish();
}
