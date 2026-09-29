#pragma once

#include "wgpupixel.h"

namespace wgpupixel::io {

// Synchronous file helpers allocate and release their own CPU and staging memory.
// load returns a caller-owned linear premultiplied float32 Image. Integer inputs
// retain 8/16-bit precision; half/float EXR and float TIFF retain HDR/negative RGB.
// Untagged integer inputs use sRGB; untagged float TIFF and EXR use linear RGB.
// ICC profiles and supported transfer tags are converted to sRGB/Rec.709 primaries.
// EXR alpha is premultiplied; TIFF follows ExtraSamples; PNG alpha is straight.
// save writes an 8-bit RGBA PNG marked as sRGB.
[[nodiscard]] WGPUPIXEL_API Image load(Context& context, std::string_view path);
WGPUPIXEL_API void save(Context& context, const Image& image, std::string_view path);

} // namespace wgpupixel::io
