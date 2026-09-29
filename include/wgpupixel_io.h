#pragma once

#include "wgpupixel.h"

namespace wgpupixel::io {

// Synchronous file helpers allocate and release their own CPU and staging memory.
// load returns a caller-owned Image after conversion through 8-bit SDR sRGB RGBA:
// input precision beyond 8 bits and HDR/negative RGB are not preserved.
// save writes an 8-bit RGBA PNG marked as sRGB.
[[nodiscard]] WGPUPIXEL_API Image load(Context& context, std::string_view path);
WGPUPIXEL_API void save(Context& context, const Image& image, std::string_view path);

} // namespace wgpupixel::io
