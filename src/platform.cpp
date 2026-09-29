#include "runtime.h"

namespace wgpupixel::detail {
WGPUInstance create_instance() {
#ifdef __EMSCRIPTEN__
    const auto feature = WGPUInstanceFeatureName_TimedWaitAny;
    WGPUInstanceDescriptor descriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;
    descriptor.requiredFeatureCount = 1;
    descriptor.requiredFeatures = &feature;
    return wgpuCreateInstance(&descriptor);
#else
    return wgpuCreateInstance(nullptr);
#endif
}

void await_future(State& state, WGPUFuture future) {
#ifdef __EMSCRIPTEN__
    WGPUFutureWaitInfo info{future, false};
    // Asyncify yields to the browser; it never blocks its event loop.
    if (wgpuInstanceWaitAny(state.instance, 1, &info, UINT64_MAX) != WGPUWaitStatus_Success || !info.completed)
        fail(ErrorCode::execution_failed, "wait", {}, "WebGPU asynchronous request failed");
#else
    (void)state;
    (void)future; // The pinned native backend completes requests and scopes synchronously.
#endif
}

std::uint64_t submit_queue(State& state, WGPUCommandBuffer command) {
#ifdef __EMSCRIPTEN__
    wgpuQueueSubmit(state.queue, 1, &command);
    return 0;
#else
    return wgpuQueueSubmitForIndex(state.queue, 1, &command);
#endif
}

void drain(State& state, const Flight* flight) {
#ifdef __EMSCRIPTEN__
    if (flight) {
        if (!flight->done.load()) await_future(state, flight->completion);
    } else {
        // Wait each callback before releasing the userdata owned by State.
        for (const auto& pending : state.flights)
            if (!pending->done.load()) await_future(state, pending->completion);
    }
#else
    if (state.asynchronous_error.load() == ErrorCode::device_lost) return;
    wgpuDevicePoll(state.device, true, flight && flight->index ? &flight->index : nullptr);
#endif
}
}
