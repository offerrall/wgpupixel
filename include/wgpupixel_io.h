#pragma once

#include "wgpupixel.h"

namespace wgpupixel::io {

// Synchronous file helpers allocate and release their own CPU and staging memory.
// load returns a caller-owned linear premultiplied float32 Image. Integer inputs
// retain 8/16-bit precision; 16-bit ICC conversion outputs linear float32, retaining
// out-of-sRGB-gamut RGB for matrix/shaper profiles. Profile tables retain their own
// precision/domain limits. Half/float EXR and linear float TIFF retain HDR/negative RGB.
// Float ICC requires XYZ matrix/shaper profiles with parametric TRCs and no input
// LUTs; table TRCs/LUT profiles fail with io_failed. Nonlinear ICC follows LittleCMS
// TRCs (sRGB extends its linear toe below zero; pure gamma clamps negatives).
// ICC input/output samples must be finite with magnitude <= 1e18, else io_failed.
// Untagged integer inputs, including 8-bit gray TIFF, use sRGB (not gamma 2.2);
// untagged float TIFF and EXR use linear RGB. ICC and transfer tags take precedence.
// Colors are converted to sRGB/Rec.709 primaries. EXR alpha is premultiplied; TIFF
// follows ExtraSamples, including 8-bit associated alpha; PNG alpha is straight.
// Associated nonlinear RGB is unpremultiplied before decoding and premultiplied
// in linear light; linear associated float RGB keeps color at zero alpha, including ICC.
// Unreadable optional TIFF EXIF leaves alpha association unspecified (straight).
// save writes an 8-bit RGBA PNG marked as sRGB.
[[nodiscard]] WGPUPIXEL_API Image load(Context& context, std::string_view path);
WGPUPIXEL_API void save(Context& context, const Image& image, std::string_view path);

} // namespace wgpupixel::io
