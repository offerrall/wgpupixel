#pragma once
#include "test.h"
#include <wgpupixel_webgpu.h>
#include <webgpu/wgpu.h>

namespace test {
using namespace wgpupixel;
template <class Handle, auto Release> struct Native {
    Handle value{};
    ~Native() {
        if (value) {
            Release(value);
        }
    }
};

struct Target {
    Native<WGPUTexture, wgpuTextureRelease> texture;
    Native<WGPUTextureView, wgpuTextureViewRelease> view;
    std::uint32_t width, height;

    Target(webgpu::NativeContext gpu, std::uint32_t w, std::uint32_t h,
           WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm)
        : width(w), height(h) {
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        desc.size = {w, h, 1};
        desc.format = format;
        texture.value = wgpuDeviceCreateTexture(gpu.device, &desc);
        test::check(texture.value != nullptr, "target texture creation failed");
        view.value = wgpuTextureCreateView(texture.value, nullptr);
        test::check(view.value != nullptr, "target view creation failed");
    }

    std::vector<std::uint8_t> read(webgpu::NativeContext gpu) const {
        const auto stride = (width * 4 + 255) & ~std::uint32_t{255};
        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        desc.size = std::uint64_t(stride) * height;
        Native<WGPUBuffer, wgpuBufferRelease> buffer{wgpuDeviceCreateBuffer(gpu.device, &desc)};
        Native<WGPUCommandEncoder, wgpuCommandEncoderRelease> encoder{
            wgpuDeviceCreateCommandEncoder(gpu.device, nullptr)};
        WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        source.texture = texture.value;
        WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        destination.buffer = buffer.value;
        destination.layout.bytesPerRow = stride;
        destination.layout.rowsPerImage = height;
        const WGPUExtent3D extent{width, height, 1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder.value, &source, &destination, &extent);
        Native<WGPUCommandBuffer, wgpuCommandBufferRelease> commands{
            wgpuCommandEncoderFinish(encoder.value, nullptr)};
        wgpuQueueSubmit(gpu.queue, 1, &commands.value);
        WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Error;
        WGPUBufferMapCallbackInfo callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowProcessEvents;
        callback.userdata1 = &status;
        callback.callback = [](WGPUMapAsyncStatus result, WGPUStringView, void* data, void*) {
            *static_cast<WGPUMapAsyncStatus*>(data) = result;
        };
        wgpuBufferMapAsync(buffer.value, WGPUMapMode_Read, 0, desc.size, callback);
        wgpuDevicePoll(gpu.device, true, nullptr);
        test::check(status == WGPUMapAsyncStatus_Success, "target readback mapping failed");
        const auto* bytes = static_cast<const std::uint8_t*>(
            wgpuBufferGetConstMappedRange(buffer.value, 0, desc.size));
        std::vector<std::uint8_t> pixels(std::size_t(width) * height * 4);
        for (std::uint32_t y = 0; y < height; ++y) {
            std::copy_n(bytes + std::size_t(y) * stride, std::size_t(width) * 4,
                        pixels.data() + std::size_t(y) * width * 4);
        }
        wgpuBufferUnmap(buffer.value);
        return pixels;
    }
};

} // namespace test
