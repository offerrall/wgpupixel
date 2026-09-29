#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace wgpupixel::detail {

enum class Kernel : std::uint8_t {
#define WGPUPIXEL_KERNEL(name) name,
#include "kernel_list.inc"
#undef WGPUPIXEL_KERNEL
    count
};

struct alignas(16) Parameters {
    std::array<std::uint32_t, 4> dimensions{}; // source width/height, destination width/height
    std::array<float, 4> values{};             // fill RGBA, or brightness/sigma/opacity at [0]
    std::array<std::int32_t, 4> offsets{};     // blend x/y, blur radius at [2]
    std::array<std::uint32_t, 4> reserved{};
    std::array<float, 4> color1{}, color2{}, extra{}, extra2{};
    std::array<std::uint32_t, 4> dispatch{}; // invocation origin x/y and extent width/height
};
static_assert(sizeof(Parameters) == 144);

struct Shader {
    std::string_view label;
    std::string_view source;
    std::string_view masked_source;
    bool data = false; // Declares @binding(7): the variable-length data channel.
};

// WGSL is embedded by CMake, including the shared parameter layout.
#define WGPUPIXEL_KERNEL(name) Shader name##_shader() noexcept;
#include "kernel_list.inc"
#undef WGPUPIXEL_KERNEL
Shader present_shader() noexcept;

inline Shader kernel_shader(Kernel kernel) noexcept {
    switch (kernel) {
#define WGPUPIXEL_KERNEL(name)                                                                     \
    case Kernel::name:                                                                             \
        return name##_shader();
#include "kernel_list.inc"
#undef WGPUPIXEL_KERNEL
    default:
        return {};
    }
}
inline bool kernel_data(Kernel kernel) noexcept {
    return kernel_shader(kernel).data;
}

} // namespace wgpupixel::detail
