#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_WIN32) && defined(WGPUPIXEL_SHARED)
#if defined(WGPUPIXEL_BUILDING_LIBRARY)
#define WGPUPIXEL_API __declspec(dllexport)
#else
#define WGPUPIXEL_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#define WGPUPIXEL_API __attribute__((visibility("default")))
#else
#define WGPUPIXEL_API
#endif

namespace wgpupixel {
namespace detail {
struct State;
struct Resource;
struct Recording;
struct Flight;
struct Presentation;
struct DisplayState;
struct StrokeState;
struct WorkspaceStorage;
struct WorkspaceAccess;
} // namespace detail
namespace webgpu {
class Presenter;
class Display;
} // namespace webgpu

enum class ErrorCode : std::uint32_t {
    invalid_argument = 1,
    invalid_resource = 2,
    capacity = 3,
    resource_busy = 4,
    out_of_memory = 5,
    device_lost = 6,
    execution_failed = 7,
    io_failed = 8
};

class WGPUPIXEL_API Error : public std::exception {
  public:
    Error(ErrorCode code, std::string_view operation, std::string_view parameter,
          std::string_view message) noexcept;
    [[nodiscard]] ErrorCode code() const noexcept;
    [[nodiscard]] std::string_view operation() const noexcept;
    [[nodiscard]] std::string_view parameter() const noexcept;
    [[nodiscard]] const char* what() const noexcept override;

  private:
    ErrorCode code_;
    std::array<char, 64> operation_{}, parameter_{};
    std::array<char, 1024> message_{};
};

// Encoding specifies the transfer curve; each operation documents clipping separately.
enum class ColorEncoding : std::uint32_t { linear, srgb };

// Linear premultiplied RGBA. RGB may be negative or HDR; alpha is in [0, 1].
// Transparent black is {0, 0, 0, 0}. Operations that divide by alpha treat alpha
// zero as black; float transfers preserve the stored bits, including RGB at alpha zero.
struct Color {
    float r, g, b, a;
};
// UI colors (pickers, swatches) are straight sRGB-encoded RGBA. These helpers apply the
// piecewise sRGB curve of RGBA8/RGBA16 uploads and downloads in float32, with the same
// constants, thresholds and order: encoded <= 0.04045f is linear / 12.92, else
// ((encoded + 0.055) / 1.055)^2.4; the inverse divides by alpha first and switches at
// linear 0.0031308f. WGSL allows an approximate pow, so the GPU transfers agree within a
// few float ulps (tested: 4e-6 relative, one 8/16-bit step at a rounding tie), not bit
// for bit. Like those transfers, channels and alpha are clamped to [0, 1]; scale the
// linear result for HDR. NaN propagates, except that zero alpha (from_srgb: after
// clamping; to_srgb: alpha <= 0) always gives {0, 0, 0, 0}. from_srgb premultiplies;
// to_srgb returns straight {r, g, b, a}.
[[nodiscard]] WGPUPIXEL_API Color from_srgb(float r, float g, float b, float a = 1) noexcept;
[[nodiscard]] WGPUPIXEL_API std::array<float, 4> to_srgb(Color color) noexcept;
// Integer pixel indices/offsets, x right and y down.
struct Position {
    std::int32_t x, y;
};
// Half-open pixel rectangle [x, x + width) x [y, y + height).
struct Rect {
    std::int32_t x, y, width, height;
};
// Continuous image coordinates: origin at the top-left corner, y down. Pixel (i, j)
// covers [i, i + 1) x [j, j + 1); its center is (i + 0.5, j + 0.5).
struct Point {
    float x, y;
    bool operator==(const Point&) const = default;
};

namespace detail {
// Constexpr sine/cosine of degrees; right angles are exact.
constexpr std::array<double, 2> sin_cos_degrees(double degrees) noexcept {
    constexpr auto largest = std::numeric_limits<double>::max();
    if (!(degrees >= -largest && degrees <= largest)) {
        const auto nan = std::numeric_limits<double>::quiet_NaN();
        return {nan, nan};
    }
    // Binary long division keeps the exact remainder of finite float angles,
    // without an overflowing integer conversion or a non-constexpr math call.
    const bool negative = degrees < 0;
    double remainder = negative ? -degrees : degrees;
    double period = 360;
    while (period <= remainder / 2) period *= 2;
    while (period >= 360) {
        if (remainder >= period) remainder -= period;
        period *= 0.5;
    }
    degrees = negative ? -remainder : remainder;
    if (degrees > 180) degrees -= 360;
    if (degrees < -180) degrees += 360;
    if (degrees == 0) return {0, 1};
    if (degrees == 90) return {1, 0};
    if (degrees == -90) return {-1, 0};
    if (degrees == 180 || degrees == -180) return {0, -1};
    const double x = degrees * (3.14159265358979323846 / 180.0);
    double sine = 0, cosine = 0, term = 1;
    for (int n = 0; n < 40; ++n) {
        // term is x^n / n!; even powers feed cosine, odd powers feed sine.
        const double signed_term = (n / 2) % 2 ? -term : term;
        (n % 2 ? sine : cosine) += signed_term;
        term *= x / (n + 1);
    }
    return {sine, cosine};
}
} // namespace detail

// 2D affine transform mapping source points to destination points, both in the
// continuous coordinates described at Point:
//   x' = a * x + c * y + e
//   y' = b * x + d * y + f
// Matrix form [a c e; b d f; 0 0 1] (the SVG/Canvas matrix(a, b, c, d, e, f) order).
// Operations that resample evaluate the inverse at destination pixel centers.
// Angles are degrees; positive rotation is clockwise on screen because y points down.
struct Affine {
    float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    bool operator==(const Affine&) const = default;

    [[nodiscard]] static constexpr Affine identity() noexcept { return {}; }
    [[nodiscard]] static constexpr Affine translate(float x, float y) noexcept {
        return {1, 0, 0, 1, x, y};
    }
    [[nodiscard]] static constexpr Affine scale(float x, float y) noexcept {
        return {x, 0, 0, y, 0, 0};
    }
    [[nodiscard]] static constexpr Affine scale(float factor) noexcept {
        return scale(factor, factor);
    }
    // Rotation about the origin, or about center when supplied. Finite degrees
    // wrap modulo 360; non-finite angles produce non-finite matrix coefficients.
    [[nodiscard]] static constexpr Affine rotate(float degrees) noexcept {
        const auto [sine, cosine] = detail::sin_cos_degrees(degrees);
        const auto s = static_cast<float>(sine), k = static_cast<float>(cosine);
        return {k, s, -s, k, 0, 0};
    }
    [[nodiscard]] static constexpr Affine rotate(float degrees, Point center) noexcept {
        const auto r = rotate(degrees);
        return {r.a, r.b, r.c, r.d, center.x - (r.a * center.x + r.c * center.y),
                center.y - (r.b * center.x + r.d * center.y)};
    }
    [[nodiscard]] constexpr Point map(Point p) const noexcept {
        return {a * p.x + c * p.y + e, b * p.x + d * p.y + f};
    }
};

// Composition: (outer * inner).map(p) == outer.map(inner.map(p)); inner applies first.
// So Affine::translate(10, 0) * Affine::rotate(90) rotates, then translates.
[[nodiscard]] constexpr Affine operator*(const Affine& outer, const Affine& inner) noexcept {
    return {outer.a * inner.a + outer.c * inner.b, outer.b * inner.a + outer.d * inner.b,
            outer.a * inner.c + outer.c * inner.d, outer.b * inner.c + outer.d * inner.d,
            outer.a * inner.e + outer.c * inner.f + outer.e,
            outer.b * inner.e + outer.d * inner.f + outer.f};
}

// Inverse computed in double precision. Empty when the transform is singular or not
// finite, or when the inverse does not fit finite floats.
[[nodiscard]] constexpr std::optional<Affine> inverse(const Affine& m) noexcept {
    const double det = double(m.a) * m.d - double(m.b) * m.c;
    if (!(det != 0 && det - det == 0)) {
        return std::nullopt;
    }
    const double values[] = {m.d / det,
                             -m.b / det,
                             -m.c / det,
                             m.a / det,
                             (double(m.c) * m.f - double(m.d) * m.e) / det,
                             (double(m.b) * m.e - double(m.a) * m.f) / det};
    constexpr double largest = 3.4028234663852886e38;
    for (const double value : values) {
        if (!(value >= -largest && value <= largest)) {
            return std::nullopt;
        }
    }
    return Affine{float(values[0]), float(values[1]), float(values[2]),
                  float(values[3]), float(values[4]), float(values[5])};
}

// Linear premultiplied color at position in [0, 1]; stops are ordered by position.
struct GradientStop {
    float position;
    Color color;
};
// How sampling treats coordinates outside the source image.
enum class EdgeMode : std::uint32_t { transparent, clamp, repeat, mirror };

// Positive dimensions fitting int32, further bounded by Context::limits().
struct ImageSize {
    std::int64_t width{}, height{};
    bool operator==(const ImageSize&) const = default;
};
// A reusable layout of independent GPU buffers. Queries describe the slots they
// need, sorted largest first; merge takes the maximum of each corresponding slot
// for sequential operations.
// bytes includes alignment. An empty plan needs no GPU storage.
class WGPUPIXEL_API WorkspacePlan {
  public:
    WorkspacePlan() = default;
    [[nodiscard]] std::uint64_t bytes() const noexcept;
    void merge(const WorkspacePlan& other);

  private:
    std::vector<std::uint64_t> slots_;
    friend struct detail::WorkspaceAccess;
};
// Result of a *_requirements query: the destination size the operation expects and
// the plan to reserve with Context::create_workspace and pass in options.workspace.
// Queries validate the sizes and options needed to compute the plan, without
// allocating; masks, regions, workspace handles and other options are checked
// when recording.
struct OperationRequirements {
    ImageSize destination{};
    WorkspacePlan workspace;
};

// Caller-owned temporary GPU storage, reusable across ordered operations on the
// same context. No operation grows it. Contents are unspecified after each call.
// Copying aliases storage; records retain it until retirement, and the last owner
// releases it. destroy rejects storage referenced by records or pending work and
// invalidates every alias.
// A default workspace has zero capacity and is valid for an empty requirement.
class WGPUPIXEL_API Workspace {
  public:
    Workspace() noexcept = default;
    [[nodiscard]] std::uint64_t capacity() const;

  private:
    std::shared_ptr<detail::WorkspaceStorage> storage_;
    explicit Workspace(std::shared_ptr<detail::WorkspaceStorage> storage);
    friend class Context;
    friend class Commands;
};

// Resampling filters. When an operation reduces the image (resize, transform,
// perspective), bilinear, bicubic (Catmull-Rom) and lanczos (3 lobes) widen with the
// reduction so each output pixel averages its whole footprint; area weighs each
// source pixel by its exact overlap with the destination pixel mapped back into the
// source (box average, also for enlargement and rotation). nearest always
// point-samples. rotate and zoom accept nearest through lanczos only.
// Large reductions are split into bounded passes over temporary images that the
// commands own until their submission retires:
// - resize runs one exact pass per axis when a direct footprint would exceed 4096 taps
//   (2 commands; temporary at most the source size).
// - transform with area integrates footprints wider than 64 source pixels (transparent
//   edges) or 6 (clamp, repeat, mirror) exactly, row by row, from a table of per-row
//   partial sums (3 commands; temporary the size of the source). Tiled edges (repeat,
//   mirror) do so while the pixels actually computed (the region, else the destination)
//   fit a work budget of about px * footprint * (4 + footprint / 64) <= 2^24 (e.g. 500 x
//   500 pixels at 1/65 scale); larger tiled requests use the approximation below, so
//   for exact results compute large tiled canvases region by region (one command
//   buffer each keeps every submission short).
//   Otherwise (bilinear / bicubic / lanczos footprints beyond 32 / 16 / 10.7 source
//   pixels with transparent edges or 12 / 6 / 4 with the others, and larger tiled area
//   reductions), the source is first box-averaged until the footprint fits within half
//   the limit (up to 3 commands; temporary the reduced size): source pixels within one
//   averaged pixel of an output pixel boundary are shared with the neighbor.
// - projective transform and perspective build a 2x box pyramid and samples each pixel on the level
// where its
//   footprint fits (1 + log2 of the larger source side commands at most; temporary a
//   third of the source). Pyramid levels blur across footprints much narrower than long
//   (steep foreshortening) and, with transparent edges, soften the layer border there
//   by up to the level's pixel size.
// With transparent edges, a layer smaller than its footprint keeps its exact coverage.
// Temporaries are float RGBA.
enum class ResizeFilter : std::uint32_t {
    nearest = 0,
    bilinear = 1,
    bicubic = 2,
    lanczos = 3,
    area = 4
};
enum class BlendMode : std::uint32_t {
    normal = 0,
    multiply = 1,
    screen = 2,
    overlay = 3,
    darken = 4,
    lighten = 5,
    difference = 6,
    exclusion = 7,
    add = 8,
    soft_light = 9,
    hard_light = 10,
    color_dodge = 11,
    color_burn = 12,
    linear_burn = 13,
    linear_dodge = 14,
    linear_light = 15,
    vivid_light = 16,
    pin_light = 17,
    hard_mix = 18,
    subtract = 19,
    divide = 20,
    darker_color = 21,
    lighter_color = 22,
    hue = 23,
    saturation = 24,
    color = 25,
    luminosity = 26,
    dissolve = 27
};

enum class FlipDirection : std::uint32_t { horizontal, vertical, both };
enum class MaskMode : std::uint32_t { alpha, luminance };
// CPU layout of image transfer buffers; rows are tightly packed, channels RGBA.
// Mask buffers always hold one 8-bit linear coverage byte per pixel.
// rgba8: straight-alpha sRGB bytes (default).
// rgba16: straight-alpha sRGB, little-endian 16-bit unsigned normalized channels.
// rgba32_float: linear premultiplied float32, identical to GPU storage; lossless and
// keeps HDR/negative values.
enum class TransferFormat : std::uint32_t { rgba8, rgba16, rgba32_float };

class Mask;

// Plain aggregate options, passed by const reference. Regions are owned values. Mask
// pointers and spans borrow inputs only while recording; commands retain GPU resources.
// Floating controls must be finite. Linear color edits retain HDR/negative results
// when representable; fully transparent pixels become transparent black unless
// the operation is a direct replacement/transfer or explicitly preserves raw bits.
struct TransferBufferOptions {
    TransferFormat format = TransferFormat::rgba8;
    std::uint64_t capacity_pixels = 0; // Zero reserves the image's pixel capacity.
};

// A8 coverage transfer storage.
struct MaskTransferBufferOptions {
    std::uint64_t capacity_pixels = 0; // Zero reserves the mask's pixel capacity; format is A8.
};

// Image and mask transfers cover the logical image, or only region, which must lie inside it.
// The buffer holds region.width * region.height tightly packed pixels.
struct TransferOptions {
    std::optional<Rect> region{};
};

// One transfer for encoded image channels or linear mask coverage. Input endpoints
// are in [0,1] with input_black < input_white; output endpoints may be reversed.
struct LevelsTransfer {
    float input_black = 0, input_white = 1; // Photoshop 0..255 divided by 255.
    float gamma = 1; // Photoshop midtone value, [0.01, 9.99]; exponent is 1/gamma.
    float output_black = 0, output_white = 1; // [0, 1]; reversed output is allowed.
};

// Mask edits blend the computed coverage with the destination through mask, round
// to A8, and clip region to the destination. The write-control mask must be distinct
// from the destination and match its size. Pixels outside region remain unchanged.
struct CopyOptions {
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct FillOptions {
    Color color{};
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct InvertOptions {
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

// ---- generation options ----
struct CheckerboardOptions {
    std::int64_t size{}; // Square side in pixels, [1, INT32_MAX].
    Color first{};
    Color second{};
    Position offset = {0, 0};
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct GridOptions {
    std::int64_t spacing{};    // Pixels between lines, [1, INT32_MAX].
    std::int64_t line_width{}; // Pixels, [1, spacing].
    Color color{};
    Color background{};
    Position offset = {0, 0};
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct StripesOptions {
    float angle{};   // Clockwise degrees, wrapping modulo 360.
    float spacing{}; // Period in pixels, at least 1.
    float width{};   // First-color stripe width in pixels, [0, spacing].
    Color first{};
    Color second{};
    float offset = 0.0f; // Pixels along the stripe pattern's varying axis.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct DotsOptions {
    float spacing{}; // Grid period in pixels, at least 1.
    float radius{};  // Pixels, [0, spacing / 2].
    Color color{};
    Color background{};
    Position offset = {0, 0};
    float softness = 0.0f; // Nonnegative transition width in pixels; at least 1 is used for AA.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct CircleOptions {
    Color color{};
    Color background{};
    float softness = 0.0f; // Nonnegative transition width in pixels; at least 1 is used for AA.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct NoiseOptions {
    std::uint32_t seed = 0;
    bool monochrome = true;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

// At every octave, mapped coordinates (pixel index + offset) / scale times
// lacunarity^octave must stay within [-2^20, 2^20]; otherwise recording rejects scale.
struct PerlinOptions {
    float scale = 1.0f; // Pixels per base noise coordinate; at least the minimum normal float.
    std::uint32_t seed{};
    std::int64_t octaves = 1; // [1, 16].
    float persistence = 0.5f; // Amplitude multiplier per octave, [0, 1].
    float lacunarity = 2.0f;  // Frequency multiplier per octave, at least 1; powers stay finite.
    Color first{};
    Color second{};
    float offset_x = 0.0f; // Pixels before applying scale.
    float offset_y = 0.0f; // Pixels before applying scale.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct PolygonOptions {
    std::int64_t sides{}; // [3, 1024].
    Color color{};
    Color background{};
    float rotation = 0.0f; // Clockwise degrees, wrapping modulo 360.
    float softness = 0.0f; // Nonnegative transition width in pixels; at least 1 is used for AA.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};
// ---- end generation options ----

// ---- compositing options ----
struct OpacityOptions {
    float factor = 1.0f;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

// Multiply destination premultiplied RGBA by source coverage at position.
// Pixels outside the placed source mask are unchanged.
struct ApplyMaskOptions {
    Position position{};
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

// Blend math uses straight linear RGB, then premultiplied source-over. Inputs are
// not pre-clamped. For saturating separable modes, the usual 0/1 output bounds
// extend to min(0,Cb)/max(1,Cb), retaining HDR and signed backdrop neutral identities.
// Dodge/burn clamp the source divisor control to [0,1]; divide bounds it below at 0.
// Burn never raises a backdrop channel; dodge never lowers one, including HDR/negative
// channels. Vivid light inherits these bounds on its burn/dodge branches.
// linear_dodge saturates at max(1,Cb); add remains the unrestricted sum.
// Hard mix is deliberately binary. Darker/lighter color compare RGB sums, ties
// retaining the backdrop. Non-separable modes use W3C Lum/Sat/ClipColor; HDR inputs
// share an affine normalization of their joint RGB range (including [0,1]) before
// that calculation, then restore the range. This is an HDR extension, not Photoshop
// document-profile emulation.
// soft_light (mode 9) uses W3C's cubic/square-root formula. For opaque gray
// Cb=.1, Cs=.9 it gives .2568.
// Blend If Gray split sliders: black/black_split fade in; white_split/white fade out.
// Gray is .30 R + .59 G + .11 B after clamping straight RGB to [0,1] and applying
// the selected encoding to each channel. Values are ordered in [0,1]; equal
// endpoints give inclusive hard cutoffs. Both range coverages multiply.
// Transparent destination pixels deliberately have gray 0: an underlying black
// cutoff can hide the source over transparency. Photoshop parity is not guaranteed.
// For an sRGB document's 0..255 sliders, select srgb and divide by 255. The default
// linear encoding instead accepts linear thresholds; 128 sRGB is about .21586 linear.
struct BlendIfRange {
    float black = 0, black_split = 0, white_split = 1, white = 1;
};
struct BlendIfOptions {
    BlendIfRange source{}, destination{};
    ColorEncoding encoding = ColorEncoding::linear;
};

// source_mask matches the source size and moves with its position. mask and region
// remain in destination coordinates. preserve_alpha implements Lock Transparent
// Pixels: Da * mix(Cb, B(Cb,Cs), As), with exactly the original destination alpha.
// For an isolated clipping group, copy the base to a temporary image, blend each
// clipped layer with preserve_alpha=true, then blend the completed group onto the
// document. Locking the already-composited document would use the whole stack's alpha.
// Dissolve thresholds source alpha * opacity * source_mask * Blend If using a hash
// of source coordinates, so its pattern moves with the layer. An explicit seed,
// including 0, is repeatable. Omitted seeds hash source resource identity with the
// ordinal of the unseeded dissolve source in this Commands recording (blend_many
// counts each source). The ordinal resets after submission, so re-recording the same
// stack with retained images is stable, including reuse of Commands. Duplicate
// sources within one recording get different patterns; distinct images also differ.
// Use explicit per-layer seeds to preserve patterns across resource recreation,
// layer reordering or different command partitioning. The same source at the same
// ordinal in separate recordings intentionally shares a pattern.
// Destination mask remains final write coverage. Fill opacity and knockout are not
// modeled; consumers must manage effects and knockout group boundaries separately.
struct BlendLayerOptions {
    const Mask* source_mask = nullptr;
    bool preserve_alpha = false;
    std::optional<std::uint32_t> seed{};
    std::optional<BlendIfOptions> blend_if{};
};

// Flat fields retain blend's designated-initializer API; controls match BlendLayerOptions.
struct BlendOptions {
    Position position{};
    float opacity = 1.0f;
    BlendMode mode = BlendMode::normal;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
    const Mask* source_mask = nullptr;
    bool preserve_alpha = false;
    std::optional<std::uint32_t> seed{};
    std::optional<BlendIfOptions> blend_if{};
};

// Empty/default/seed-only layers retain four-source batching. A source mask, alpha
// lock or Blend If anywhere in layers selects one record per source for this call.
// All supplied spans use source order.
struct BlendManyOptions {
    std::span<const Position> positions{};
    std::span<const float> opacities{};
    std::span<const BlendMode> modes{};
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
    std::span<const BlendLayerOptions> layers{};
};

// ---- end compositing options ----

// ---- geometry options ----
struct ResizeOptions {
    ResizeFilter filter = ResizeFilter::bilinear;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
    Workspace workspace{}; // Sized by resize_requirements.
};

struct FlipOptions {
    FlipDirection direction = FlipDirection::horizontal;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct RotateOptions {
    float degrees{};
    ResizeFilter filter = ResizeFilter::bilinear;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct CropOptions {
    Position origin{};
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

// Fixed-support resampling with transparent edges; factor is finite and positive.
// center uses continuous source coordinates: {width/2, height/2} is the canvas center.
struct ZoomOptions {
    float factor = 1.0f;
    Point center{}; // Source point placed at the destination center.
    ResizeFilter filter = ResizeFilter::bilinear;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

// Projective transform (homography) mapping source points to destination points in
// the continuous coordinates described at Point; row-major 3x3, double precision:
//   x' = (m[0] x + m[1] y + m[2]) / w,  y' = (m[3] x + m[4] y + m[5]) / w,
//   w = m[6] x + m[7] y + m[8].
struct Homography {
    std::array<double, 9> m{1, 0, 0, 0, 1, 0, 0, 0, 1};
    bool operator==(const Homography&) const = default;

    [[nodiscard]] static constexpr Homography from(const Affine& t) noexcept {
        return {{t.a, t.c, t.e, t.b, t.d, t.f, 0, 0, 1}};
    }
    [[nodiscard]] constexpr Point map(Point p) const noexcept {
        const double w = m[6] * p.x + m[7] * p.y + m[8];
        return {static_cast<float>((m[0] * p.x + m[1] * p.y + m[2]) / w),
                static_cast<float>((m[3] * p.x + m[4] * p.y + m[5]) / w)};
    }
};

// Composition as for Affine: the right operand applies first.
[[nodiscard]] constexpr Homography operator*(const Homography& outer,
                                             const Homography& inner) noexcept {
    Homography result;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            result.m[row * 3 + column] = outer.m[row * 3] * inner.m[column] +
                                         outer.m[row * 3 + 1] * inner.m[3 + column] +
                                         outer.m[row * 3 + 2] * inner.m[6 + column];
        }
    }
    return result;
}

// Matrix inverse without arbitrary homogeneous rescaling. Empty when the matrix
// is singular or not finite, or the inverse cannot be represented as finite doubles.
// Extremely unequal coefficient magnitudes may still exceed intermediate range.
[[nodiscard]] constexpr std::optional<Homography> inverse(const Homography& h) noexcept {
    constexpr auto largest = std::numeric_limits<double>::max();
    double scale = 0;
    for (const double value : h.m) {
        if (!(value >= -largest && value <= largest)) return std::nullopt;
        const double magnitude = value < 0 ? -value : value;
        if (magnitude > scale) scale = magnitude;
    }
    if (scale == 0) return std::nullopt;
    const auto calculate = [](const std::array<double, 9>& m,
                              double scale) constexpr -> std::optional<Homography> {
        // Runtime keeps the ordinary floating-point operations. Constant evaluation
        // must detect overflow before performing it so the same fallback is reachable.
        bool valid = true;
        const auto multiply = [&](double a, double b) constexpr {
            if consteval {
                const double aa = a < 0 ? -a : a, bb = b < 0 ? -b : b;
                if (bb > 1 && aa > largest / bb) {
                    valid = false;
                    return 0.0;
                }
            }
            return a * b;
        };
        const auto add = [&](double a, double b) constexpr {
            if consteval {
                if ((b > 0 && a > largest - b) || (b < 0 && a < -largest - b)) {
                    valid = false;
                    return 0.0;
                }
            }
            return a + b;
        };
        const auto divide = [&](double a, double b) constexpr {
            if consteval {
                const double aa = a < 0 ? -a : a, bb = b < 0 ? -b : b;
                if (bb < 1 && aa > largest * bb) {
                    valid = false;
                    return 0.0;
                }
            }
            return a / b;
        };
        const std::array<double, 9> adjugate{
            add(multiply(m[4], m[8]), -multiply(m[5], m[7])),
            add(multiply(m[2], m[7]), -multiply(m[1], m[8])),
            add(multiply(m[1], m[5]), -multiply(m[2], m[4])),
            add(multiply(m[5], m[6]), -multiply(m[3], m[8])),
            add(multiply(m[0], m[8]), -multiply(m[2], m[6])),
            add(multiply(m[2], m[3]), -multiply(m[0], m[5])),
            add(multiply(m[3], m[7]), -multiply(m[4], m[6])),
            add(multiply(m[1], m[6]), -multiply(m[0], m[7])),
            add(multiply(m[0], m[4]), -multiply(m[1], m[3]))};
        const double det = add(add(multiply(m[0], adjugate[0]), multiply(m[1], adjugate[3])),
                               multiply(m[2], adjugate[6]));
        if (!valid || !(det != 0 && det - det == 0)) return std::nullopt;
        // Combine the denominators when this can prevent overflow of the inverse
        // of the normalized matrix, without overflowing the denominator itself.
        const double magnitude = det < 0 ? -det : det;
        const bool combined = scale > 1 && magnitude <= largest / scale;
        const double denominator = combined ? multiply(det, scale) : det;
        if (!valid) return std::nullopt;
        Homography result;
        for (int i = 0; i < 9; ++i) {
            const double value = divide(adjugate[i], denominator);
            result.m[i] = combined ? value : divide(value, scale);
            if (!valid || !(result.m[i] >= -largest && result.m[i] <= largest)) return std::nullopt;
        }
        return result;
    };
    const auto normalized = [&]() constexpr {
        // A power-of-two divisor preserves distinct adjacent significands; dividing
        // by an arbitrary largest entry can round a nonsingular matrix singular.
        double divisor = 1;
        while (divisor > scale) divisor *= 0.5;
        while (divisor <= scale / 2) divisor *= 2;
        auto m = h.m;
        for (auto& value : m) value /= divisor;
        return calculate(m, divisor);
    };
    // Preserve every inverse the ordinary route can already compute, including
    // matrices whose unequal coefficient magnitudes would underflow if normalized.
    if (const auto result = calculate(h.m, 1)) return result;
    return normalized();
}

// Photoshop Edit > Transform > Distort/Perspective: the homography taking the source
// rectangle's corners (0, 0), (width, 0), (width, height) and (0, height) to
// corners, in that order (top-left, top-right, bottom-right, bottom-left). Empty
// unless the corners are finite and form a strictly convex quadrilateral; the
// reversed winding mirrors the image.
[[nodiscard]] WGPUPIXEL_API std::optional<Homography>
perspective_matrix(ImageSize source, const std::array<Point, 4>& corners);

// Smallest pixel-aligned rectangle containing the continuous rectangle
// [x, x + width] x [y, y + height] after matrix, e.g. the layer an editor allocates
// for a transformed layer; draw into it with Affine::translate(-bounds.x, -bounds.y)
// * matrix (or corners offset by -bounds). Edges within 1/1000 pixel of an integer
// snap to it, so exact transforms do not gain an empty row or column. Throws
// ErrorCode::invalid_argument for an empty source, non-finite results, results
// outside int32, or a homography that maps part of the rectangle beyond its horizon.
[[nodiscard]] WGPUPIXEL_API Rect transform_bounds(const Rect& source, const Affine& matrix);
[[nodiscard]] WGPUPIXEL_API Rect transform_bounds(const Rect& source, const Homography& matrix);

// Photoshop Free Transform: every destination pixel center is mapped back through
// the inverse of matrix (source to destination, see Affine) and filtered. Reductions
// are antialiased by widening the filter over the pixel's footprint; large ones first
// average the source (see ResizeFilter). edge decides what lies outside the source: transparent (a layer
// transform), clamp, repeat (tiling) or mirror. Every destination pixel inside
// region/mask is replaced; blend the result to composite it.
struct TransformOptions {
    Affine matrix{};
    ResizeFilter filter = ResizeFilter::bilinear;
    EdgeMode edge = EdgeMode::transparent;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
    Workspace workspace{}; // Sized by transform_requirements.
};

// Composable projective mapping. Finite, invertible; the source rectangle must
// remain on one side of the horizon. H and any nonzero scalar multiple are equivalent.
struct ProjectiveTransformOptions {
    Homography matrix{};
    ResizeFilter filter = ResizeFilter::bilinear;
    EdgeMode edge = EdgeMode::transparent;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
    Workspace workspace{}; // Sized by transform_requirements.
};

// Distort/Perspective by the destination positions of the source corners (see
// perspective_matrix). Filtering and edges follow TransformOptions; destination
// pixels beyond the horizon of the projection are transparent.
struct PerspectiveOptions {
    std::array<Point, 4> corners{};
    ResizeFilter filter = ResizeFilter::bilinear;
    EdgeMode edge = EdgeMode::transparent;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
    Workspace workspace{}; // Sized by perspective_requirements.
};

// Photoshop Filter > Other > Offset: destination(x, y) = source(x - offset.x,
// y - offset.y), exact pixels. edge: repeat is Wrap Around, clamp is Repeat Edge
// Pixels, transparent is Set to Transparent; mirror reflects.
struct OffsetOptions {
    Position offset{};
    EdgeMode edge = EdgeMode::repeat;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

// Pure sizing queries. Large reductions (split resize passes, area tables, reduced
// sources and box pyramids) need the returned plan in options.workspace; the others
// return an empty plan and accept an empty workspace. The plan depends on the sizes,
// filter, edge, geometry and region, not on the mask or workspace handles, and is
// the same for image and mask overloads.
[[nodiscard]] WGPUPIXEL_API OperationRequirements
resize_requirements(ImageSize source, ImageSize destination, const ResizeOptions& options);
[[nodiscard]] WGPUPIXEL_API OperationRequirements
transform_requirements(ImageSize source, ImageSize destination, const TransformOptions& options);
[[nodiscard]] WGPUPIXEL_API OperationRequirements transform_requirements(
    ImageSize source, ImageSize destination, const ProjectiveTransformOptions& options);
[[nodiscard]] WGPUPIXEL_API OperationRequirements
perspective_requirements(ImageSize source, ImageSize destination, const PerspectiveOptions& options);
// ---- end geometry options ----

// ---- adjustments options ----
struct GrayscaleOptions {
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct BrightnessOptions {
    float amount{};
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct ExposureOptions {
    float stops{};
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct ContrastOptions {
    float factor = 1.0f;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct SaturationOptions {
    float factor = 1.0f;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct GammaOptions {
    float value = 1.0f;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct HueOptions {
    float degrees{};
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct VibranceOptions {
    float amount{}; // [-1, 1]; zero is neutral.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct SepiaOptions {
    float intensity = 1.0f; // Blend fraction, [0, 1].
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

// Compare straight linear Rec.709 luminance with value in [0,1], inclusive >=.
// Alpha is preserved; zero-alpha pixels remain transparent.
struct ThresholdOptions {
    float value = 0.5f;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct SolarizeOptions {
    float value = 0.5f;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct ChromaKeyOptions {
    Color key{}; // Positive alpha; unpremultiplied RGB must be finite and nonnegative.
    // Nonnegative distances in weighted HSV of straight linear RGB; zero threshold
    // leaves pixels unchanged. Negative straight input RGB and pixels whose
    // unpremultiplication overflows are not keyed.
    float threshold = 0.4f;
    float smoothness = 0.1f; // Transition width below threshold; zero gives a hard cutoff.
    float spill_suppression = 0.5f; // [0, 1].
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

// Color math uses straight signed extended sRGB, then decodes and re-premultiplies.
// HDR/negative RGB is retained, including active edits, unless an operation explicitly
// defines a bounded mapping below. Neutral controls record no command and preserve bits.
// Results must be representable in float32. Alpha is preserved unless stated otherwise.
// These are specified editor controls, not bit-exact Adobe implementations.
// Selective color is not provided; hue ranges and tetrahedral LUTs remain unsupported.
struct LevelsOptions {
    // Channels then composite. SDR input clips at input black/white, then gamma
    // maps into output black/white. Only x<0 or x>1 uses signed-power extension,
    // shifted to meet the clipped mapping continuously at 0/1.
    LevelsTransfer composite{}, red{}, green{}, blue{};
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};
struct CurvesOptions {
    // Empty means identity. 2..4096 points, strictly increasing x in [0, 1], y in
    // [0, 1]. Constant outside the control endpoints within [0,1]; shape-preserving
    // cubic Hermite between points (no overshoot, including nonmonotonic curves).
    // Only x<0 or x>1 extrapolates, anchored at the 0/1 values with the first/last
    // control tangent. Channels then composite.
    // Compiled to 4096 samples per curve; spans are copied while recording.
    // Endpoint slopes must fit finite float32 (rejects pathological near-coincident
    // knots). Identity curves preserve HDR/negative inputs.
    std::span<const Point> composite{}, red{}, green{}, blue{};
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};
struct LutOptions {
    // Empty is identity, otherwise 2..65536 evenly spaced samples on [0, 1].
    // Finite signed/HDR values, linear interpolation, channels then composite.
    // Extrapolate beyond [0, 1] using the first/last segment slopes; slopes must fit
    // float32. Empty/identity tables preserve the input range.
    std::span<const float> composite{}, red{}, green{}, blue{};
    ColorEncoding domain = ColorEncoding::srgb;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};
struct Lut3DOptions {
    std::uint32_t size = 2; // [2, 65]. Trilinear interpolation (no tetrahedral mode).
    // size^3 RGB triplets, red index fastest, then green, then blue (.cube order).
    // Finite signed/HDR outputs in the selected domain; sRGB uses the signed extended
    // transfer function and values must decode to finite float32. No output clipping.
    std::span<const float> values{};
    ColorEncoding domain = ColorEncoding::srgb;
    // .cube DOMAIN_MIN/MAX, in the selected domain. Inputs clamp to these bounds.
    // Each max-min must be positive, finite and normal in float32.
    std::array<float, 3> domain_min{0, 0, 0}, domain_max{1, 1, 1};
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};
struct ColorBalanceOptions {
    // Cyan/red, magenta/green, yellow/blue: [-1, 1] maps to Photoshop [-100, 100].
    // Localized encoded HSL-lightness bands: linear crossfades of width 0.25
    // centered at 1/3 and 2/3. The weights sum to 1; maximum correction is 0.7.
    std::array<float, 3> shadows{}, midtones{}, highlights{};
    // Restore encoded Rec.709 luma, then compress chroma toward that gray only
    // as needed to fit [min(0,minRGB), max(1,maxRGB)] of the input. SDR stays in
    // gamut; existing HDR/negative ranges are retained. Hue and luma are preserved.
    bool preserve_luminosity = true;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};
struct HueSaturationOptions {
    // HDR HSL uses the encoded interval [min(0,minRGB), max(1,maxRGB)], maps it
    // to [0,1] for the edit, then restores that interval. This avoids gamut clipping.
    float hue = 0; // Degrees [-180, 180]; colorize additionally accepts [0, 360].
    // [-1, 1] maps to [-100, 100]: multiply HSL S by (1 + saturation), capped at 1.
    // This is continuous at neutral gray. Only master edits are supported, not hue ranges.
    float saturation = 0;
    float lightness = 0;   // [-1, 1] maps to [-100, 100]; mix toward black/white.
    bool colorize = false; // Replace hue and use saturation in [0, 1] as absolute HSL S.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};
struct ColorMatrixOptions {
    // Row-major 4x5 on straight RGBA plus constant 1; finite coefficients [-65536, 65536].
    // Alpha result clamped to [0, 1].
    // Both domains retain negative/HDR RGB; srgb uses the signed extended transfer.
    // Transparent input uses straight RGB=0, so alpha offsets may create coverage.
    std::array<float, 20> matrix{1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0};
    ColorEncoding domain = ColorEncoding::linear;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};
struct ChannelMixerOptions {
    // RGB coefficients and constant per output channel; [-2, 2] = [-200%, 200%].
    std::array<float, 4> red{1, 0, 0, 0}, green{0, 1, 0, 0}, blue{0, 0, 1, 0};
    bool monochrome = false; // Red row supplies all three outputs.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};
struct BlackWhiteOptions {
    // Reds/yellows/greens/cyans/blues/magentas: [-2, 3] = [-200%, 300%].
    // Interpolate by encoded hue; gray = min(RGB) + chroma * hue weight, without clipping.
    // Tint uses the same expanded HSL interval as hue_saturation.
    std::array<float, 6> weights{0.4f, 0.6f, 0.4f, 0.6f, 0.2f, 0.8f};
    bool tint = false;
    Color tint_color{0.76f, 0.57f, 0.36f, 1}; // Opaque linear RGB; sets HSL hue/saturation.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};
struct PhotoFilterOptions {
    Color color{1, 0.5f, 0, 1};      // Opaque linear RGB filter color in [0, 1].
    float density = 0.25f;           // [0, 1] = Photoshop [0%, 100%]; encoded RGB multiply blend.
    bool preserve_luminosity = true; // Same luma restoration as color balance.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};
struct PosterizeOptions {
    // Bounded effect: clips encoded input to [0,1], then selects one of 2..256
    // output levels using equal-width input bins.
    std::uint32_t levels = 4;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};
struct GradientMapOptions {
    // 2..4096 nondecreasing stops in [0, 1]; constant beyond endpoints. At duplicate
    // positions the last stop wins (right-continuous hard edges). Lookup is O(log N).
    // Bounded effect: encoded Rec.709 luma of RGB clipped to [0,1] indexes the stops.
    // Input colors are linear premultiplied
    // SDR (0 <= RGB <= alpha); interpolation uses encoded premultiplied RGB and alpha.
    // Unlike Photoshop, gradient transparency multiplies source alpha; use opaque
    // stops for Photoshop-style maps. Black-to-white opaque stops preserve gray ramps.
    std::span<const GradientStop> stops{};
    bool reverse = false;
    bool dither = false; // Deterministic +/- 0.5/255 noise added to the lookup position.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};
// ---- end adjustments options ----

// ---- filters options ----
// Gaussian blur expects finite pixels; NaN/Inf propagation is unspecified.
// Supports through 128 use discrete convolution (through 64 use shared-memory tiles).
// Larger supports use a low-pass pyramid that retains endpoints, convolution at
// support <=64, and bilinear reconstruction. This approximates the Gaussian at subpixel pyramid
// precision; it preserves signed/HDR values and has no radius or image-work limit.
struct GaussianBlurOptions {
    std::int64_t radius{}; // Support radius in pixels, [0, INT32_MAX].
    float sigma = 1.0f; // Standard deviation in pixels, positive.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
    Workspace workspace{};
};

struct SharpenOptions {
    float strength = 1.0f;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct SobelOptions {
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct EmbossOptions {
    float strength = 1.0f; // Nonnegative; 2 * strength must remain finite.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct RoundedCornersOptions {
    float radius{}; // Pixels, [0, half the smaller image dimension].
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

// Drop shadow: source alpha tinted with color, blurred by a Gaussian of sigma over
// radius pixels, moved by offset and composited under the source into a cleared
// destination. expand sizes the destination to hold source, blur padding and offset
// (source plus 2 radius plus |offset| per axis); otherwise it keeps the source size
// and clips. drop_shadow_requirements gives that size and the workspace.
struct DropShadowOptions {
    Position offset{};
    std::int64_t radius{};
    float sigma = 1.0f;
    Color color{};
    bool expand = true;
    Workspace workspace{};
};

struct VignetteOptions {
    // Each axis with more than one pixel spans [-1, 1] across its pixel centers. Distances use these
    // normalized coordinates: 1 reaches a side midpoint; sqrt(2) reaches a corner.
    float radius{};   // Nonnegative distance at the transition midpoint.
    float softness{}; // Nonnegative transition width; zero gives a hard edge.
    Color color{};
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

// Clamp edge extension for every neighborhood filter. Masks blend the filtered
// result with the previous destination; regions restrict writes, never reads.
// Large supports use bounded passes over options.workspace, retained until retirement.
// Pixel arithmetic requires finite premultiplied input; NaN/Inf are unspecified.
struct BoxBlurOptions {
    std::int64_t radius = 1; // Square support, [0, 1024] pixels.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
    Workspace workspace{};
};
struct MotionBlurOptions {
    float angle = 0;    // Clockwise degrees, [-360, 360].
    // Above 32 pixels, dyadic line passes approximate dense bilinear integration.
    float distance = 0; // Centered exposure length, [0, 4096] pixels.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
    Workspace workspace{};
};
enum class RadialBlurMode : std::uint32_t { spin, zoom };
struct RadialBlurOptions {
    Point center{-1, -1}; // Both -1 select the image center; otherwise image coordinates.
    float amount = 0;    // [0, 100]: spin arc in degrees, zoom exposure in percent.
    // Long trajectories use dyadic rotations/scales with subpixel sampling. Zoom
    // integrates uniform inward exposure; spin integrates a centered arc. Repeated
    // bilinear resampling adds a small reconstruction blur; each pass clamps edges.
    RadialBlurMode mode = RadialBlurMode::spin;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
    Workspace workspace{};
};
struct UnsharpMaskOptions {
    float amount = 100;  // Photoshop Amount percent, [0, 500].
    float radius = 1;    // Gaussian sigma in pixels, [0, 1000], support ceil(3 * radius).
    // Above 32, uses the Gaussian pyramid approximation described above.
    // [0, 255] levels: per-channel sRGB-encoded difference * 255. Sharpening stays linear.
    // Negative colors use the linear extension of the sRGB toe.
    float threshold = 0;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
    Workspace workspace{};
};
struct HighPassOptions {
    float radius = 1; // As Unsharp Mask; neutral gray is 0.5 in linear light.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
    Workspace workspace{};
};
struct MedianOptions {
    // [0, 500]. Exact component-wise median of premultiplied RGBA, including alpha.
    // Radii 1–3 use fixed sorting networks; larger windows use tiled rank queries
    // over ordered float bits. Flat windows are unchanged, including signed/HDR values.
    std::int64_t radius = 1;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
    Workspace workspace{};
};
// Pure sizing query; mask, region and workspace handles are checked when recording.
// Radii above 3 require the returned plan in options.workspace. Reserve it with
// Context::create_workspace before recording; smaller radii accept an empty workspace.
[[nodiscard]] WGPUPIXEL_API OperationRequirements
median_requirements(ImageSize source, const MedianOptions& options);
// Command capacity grows automatically. Large recordings may wait for bounded
// internal batches during Context::submit; Submission tracks the remaining work.
enum class MorphologyShape : std::uint32_t { square, round };
struct MorphologyOptions {
    std::int64_t radius = 1; // [0, 500]. Square is exact; round is an integer disk through 10.
    // Larger round supports use eight line directions (a 16-sided disk approximation),
    // with radial boundary error <= 3% of radius + 4 pixels. RGBA extrema include alpha.
    MorphologyShape shape = MorphologyShape::square;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
    Workspace workspace{};
};
struct PixelateOptions {
    std::int64_t cell_size = 8; // [1, 1024]; cells anchored at image origin, clipped at edges.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};
enum class NoiseDistribution : std::uint32_t { uniform, gaussian };
struct AddNoiseOptions {
    float amount = 1; // [0, 400] percent: uniform half-width or Gaussian standard deviation.
    NoiseDistribution distribution = NoiseDistribution::uniform;
    bool monochrome = false;
    bool clip = false; // Clamp straight RGB to [0, 1]; false preserves HDR/negative values.
    std::uint32_t seed = 0;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};
struct SurfaceBlurOptions {
    std::int64_t radius = 1; // [0, 100] pixels; spatial sigma max(radius / 2, 0.5).
    float threshold = 15;   // [0, 255] levels: range sigma in straight linear RGB * 255.
    // Through radius 3, exact bilateral filtering; above 3, a multiscale separable
    // bilateral approximation guided by the original image (doubling line spacing).
    // Preserve center alpha; weight visible neighbor colors by their coverage.
    // Fully transparent centers stay transparent, independent of neighbor color.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
    Workspace workspace{};
};
// Motion, radial, and surface cascades need at most two temporary full-size images.
// Queries return plans for caller-owned GPU storage; no filter allocates it implicitly.
[[nodiscard]] WGPUPIXEL_API OperationRequirements
motion_blur_requirements(ImageSize source, const MotionBlurOptions& options);
[[nodiscard]] WGPUPIXEL_API OperationRequirements
radial_blur_requirements(ImageSize source, const RadialBlurOptions& options);
[[nodiscard]] WGPUPIXEL_API OperationRequirements
surface_blur_requirements(ImageSize source, const SurfaceBlurOptions& options);
// Source/destination images have equal dimensions and must be distinct. Queries
// validate options and geometry without GPU allocation. Masks, regions and supplied
// workspace handles are validated when recording. Create a workspace from the
// returned plan and pass it in options.workspace; merge plans for sequential reuse.
// Box blur, square minimum/maximum, unsharp mask and high pass require workspace
// even at radius zero; their regular filtering passes still run.
[[nodiscard]] WGPUPIXEL_API OperationRequirements
box_blur_requirements(ImageSize source, const BoxBlurOptions& options);
[[nodiscard]] WGPUPIXEL_API OperationRequirements
minimum_requirements(ImageSize source, const MorphologyOptions& options);
[[nodiscard]] WGPUPIXEL_API OperationRequirements
maximum_requirements(ImageSize source, const MorphologyOptions& options);
[[nodiscard]] WGPUPIXEL_API OperationRequirements
unsharp_mask_requirements(ImageSize source, const UnsharpMaskOptions& options);
[[nodiscard]] WGPUPIXEL_API OperationRequirements
high_pass_requirements(ImageSize source, const HighPassOptions& options);
[[nodiscard]] WGPUPIXEL_API OperationRequirements
gaussian_blur_requirements(ImageSize source, const GaussianBlurOptions& options);
[[nodiscard]] WGPUPIXEL_API OperationRequirements
drop_shadow_requirements(ImageSize source, const DropShadowOptions& options);
// ---- end filters options ----

// ---- selection options ----
struct MaskFillOptions {
    float coverage{};
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct ExtractMaskOptions {
    MaskMode mode = MaskMode::luminance;
};

// Selections are Masks: coverage 0 is unselected, 1 fully selected. The mode says how
// new coverage n meets the existing selection e (Photoshop's New/Add to/Subtract
// from/Intersect with selection). It is evaluated exactly on coverage bytes:
// replace n, add max(e, n), subtract max(e - n, 0), intersect min(e, n) and
// difference |e - n| (exclusive or). Identical soft edges therefore cancel or merge
// without halos.
enum class SelectionMode : std::uint32_t { replace, add, subtract, intersect, difference };
// Winding rule for polygon selections: nonzero fills every enclosed area; even_odd
// leaves areas enclosed an even number of times unselected (holes, star centers).
enum class FillRule : std::uint32_t { nonzero, even_odd };

// Marquee shapes use the continuous coordinates of Point and are then mapped by
// transform, so a marquee can be rotated, skewed or moved (Transform Selection).
// anti_alias computes exact pixel area coverage, with curves flattened to within 1/64
// pixel; without it a pixel is selected when its center is inside, like the marquee's
// Anti-alias checkbox turned off. feather (pixels, as Photoshop's Feather field) blurs
// the new shape before it is combined; it needs a workspace sized by
// feather_requirements(mask size, {.radius = feather}).
struct RectangleSelectionOptions {
    Point origin{}; // Top-left corner before transform.
    float width{};
    float height{};
    float corner_radius = 0.0f; // Rounded rectangle; limited to half the shorter side.
    Affine transform{};
    SelectionMode mode = SelectionMode::replace;
    bool anti_alias = true;
    float feather = 0.0f;
    Workspace workspace{};
};

// The ellipse inscribed in the rectangle origin, width, height (Elliptical Marquee).
struct EllipseSelectionOptions {
    Point origin{};
    float width{};
    float height{};
    Affine transform{};
    SelectionMode mode = SelectionMode::replace;
    bool anti_alias = true;
    float feather = 0.0f;
    Workspace workspace{};
};

// Lasso and Polygonal Lasso. Points form closed contours; each contour closes back
// to its first point. contours lists point counts per contour and must sum to
// points.size(); empty means one contour. Several contours make holes or islands.
// Anti-aliased coverage is exact area for any contour orientation, overlap or
// duplication, except where two edges cross inside a 1/16-row slab of a pixel (error
// below 1/16 of that pixel). Pixels met by more than 64 edge pieces are supersampled
// 16x16 with exact fill rules. Runs of points within 1/1024 pixel of a straight chord
// (dense lasso samples) are merged. Only pathological shapes, with hundreds of edges
// crossing thousands of rows at once, exceed the work budget (about a second on an
// integrated GPU) and fail with ErrorCode::capacity instead of stalling the GPU.
struct PolygonSelectionOptions {
    std::span<const Point> points{};
    std::span<const std::uint32_t> contours{};
    FillRule rule = FillRule::nonzero;
    Affine transform{};
    SelectionMode mode = SelectionMode::replace;
    bool anti_alias = true;
    float feather = 0.0f;
    Workspace workspace{};
};

// Load Selection / channel operations: combine another mask into destination.
struct MaskCombineOptions {
    SelectionMode mode = SelectionMode::add;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

// canvas_bounds matches Photoshop's "Apply effect at canvas bounds": pixels outside the
// canvas count as unselected. Otherwise a selection touching the edge stays selected.
// Feather: Gaussian blur of the selection; radius (0-1000) is the standard deviation in
// pixels (Photoshop's Feather Radius). Up to radius 16 it is an exact Gaussian sampled
// to three radii; above, the Gaussian runs on a grid reduced by a power of two, with
// the canvas edge carried at full resolution, and is reconstructed bilinearly (within
// about 1% of the exact result, edges included), so cost stays constant per pixel.
struct FeatherOptions {
    float radius{};
    bool canvas_bounds = false;
    Workspace workspace{};
};

// Select > Modify > Expand and Contract: the edge moves by radius pixels (0-2000,
// fractional allowed) along exact Euclidean distances, so corners round like
// Photoshop's. The edge position is read with sub-pixel precision from anti-aliased
// coverage (straight edges move exactly; curves within about 0.1 pixel), so small
// radii move the edge smoothly; hard selections behave as pixel squares. Coverage
// below 50% only counts when it borders the selection (an anti-aliased rim); feathered
// tails are not expanded. Existing coverage is never reduced by expand or raised by
// contract. Cost does not grow with the radius beyond the distance scan.
struct ExpandOptions {
    float radius{};
    Workspace workspace{};
};

struct ContractOptions {
    float radius{};
    bool canvas_bounds = false;
    Workspace workspace{};
};

// Select > Modify > Border: the expanded minus the contracted selection by width / 2,
// an anti-aliased band of total width (0-4000] straddling the edge.
struct BorderOptions {
    float width{};
    bool canvas_bounds = false;
    Workspace workspace{};
};

// Select > Modify > Smooth: a pixel becomes selected when most of the square
// (2 radius + 1) neighborhood is selected, removing specks and jagged steps. Radius
// 0-100 as in Photoshop.
struct SmoothOptions {
    std::int64_t radius{};
    bool canvas_bounds = false;
    Workspace workspace{};
};

// Coverage at or above value becomes fully selected, the rest unselected.
struct MaskThresholdOptions {
    float value = 0.5f; // Linear coverage in [0,1], inclusive >=.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

// Levels on the selection (Quick Mask + Image > Adjustments > Levels), coverage in
// [0, 1]: input black/white points, midtone gamma (> 1 raises midtones like
// Photoshop's middle slider moved left) and output range.
struct MaskLevelsOptions {
    LevelsTransfer transfer{};
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

// Select > Color Range with sampled colors. Colors compare as straight 8-bit-scale
// sRGB, like Photoshop in an 8-bit RGB document; HDR and negative values extend the
// sRGB curve instead of clipping, so bright highlights stay distinct. color is linear
// premultiplied like
// image pixels (for example read from the image with a partial download); its alpha
// must be positive. Coverage falls linearly from 1 at the color to 0 at a Euclidean
// RGB distance of fuzziness * sqrt(3), scaled by pixel alpha. fuzziness is in
// [0,1], where 1 spans the SDR RGB cube diagonal; HDR distances may exceed it.
struct ColorRangeOptions {
    Color color{};
    float fuzziness = 40.0f / (255.0f * 1.7320508075688772f);
    SelectionMode mode = SelectionMode::replace;
};

// Magic Wand. The reference color is the seed pixel, or the rounded average of the
// (2 sample_radius + 1)^2 square around it (Sample Size: 1 is 3x3, 2 is 5x5 ...
// 50 is 101x101). A pixel matches when every straight sRGB 8-bit-scale channel and
// alpha differs by at most tolerance * 255 (tolerance in [0,1]), channels rounded
// to integers like an RGBA8 download but extended beyond 0-255 for HDR and negative
// colors; transparent pixels compare as transparent black. contiguous selects the exact connected
// region of matching pixels containing the seed (4-connected, or 8 with diagonal); otherwise every
// matching pixel. anti_alias softens the edge by one pixel while keeping exactly the matched pixels
// above 50% coverage. Sample the merged image by passing the flattened composite.
struct MagicWandOptions {
    Position seed{};
    float tolerance = 32.0f / 255.0f;
    std::int64_t sample_radius = 0;
    bool contiguous = true;
    bool anti_alias = true;
    bool diagonal = false;
    SelectionMode mode = SelectionMode::replace;
    Workspace workspace{};
};

// Pure sizing queries: the returned plan goes in options.workspace. A zero radius
// needs no workspace. Shapes with feather use feather_requirements with the same
// radius. The destination is the mask size (the source size for the magic wand).
[[nodiscard]] WGPUPIXEL_API OperationRequirements
feather_requirements(ImageSize mask, const FeatherOptions& options);
[[nodiscard]] WGPUPIXEL_API OperationRequirements
expand_requirements(ImageSize mask, const ExpandOptions& options);
[[nodiscard]] WGPUPIXEL_API OperationRequirements
contract_requirements(ImageSize mask, const ContractOptions& options);
[[nodiscard]] WGPUPIXEL_API OperationRequirements
border_requirements(ImageSize mask, const BorderOptions& options);
[[nodiscard]] WGPUPIXEL_API OperationRequirements
smooth_requirements(ImageSize mask, const SmoothOptions& options);
[[nodiscard]] WGPUPIXEL_API OperationRequirements
select_magic_wand_requirements(ImageSize source, const MagicWandOptions& options);
// ---- end selection options ----

// ---- painting options ----
// One input event of a stroke, in image coordinates. pressure and tilt lie in [0, 1]:
// tilt 0 is an upright pen, 1 a flat one. rotation is barrel rotation or azimuth in
// degrees clockwise. Devices without pressure keep the defaults.
struct StrokeSample {
    Point position{};
    float pressure = 1.0f;
    float tilt = 0.0f;
    float rotation = 0.0f;
};

// Source of each dab's angle (Photoshop Shape Dynamics, Angle Jitter Control): the
// brush angle alone, plus the sample's rotation, or plus the stroke direction.
enum class BrushAngleControl : std::uint32_t { fixed, rotation, direction };

// A brush preset: Photoshop Brush Tip Shape, Shape Dynamics, Scattering and Transfer.
// Dabs are placed every spacing * diameter pixels (at least 0.1) along the sample
// polyline, starting at the first sample, using the pressure-driven size before jitter;
// pressure, tilt and rotation interpolate linearly in between. A stroke may place up
// to 2^22 dabs, and each tool call may evaluate up to 2^34 dab-pixel pairs (dabs times
// the pixels of the 16-256 px tiles they touch); beyond, ErrorCode::capacity asks to
// raise spacing or shorten the stroke. Disjoint output rectangles bound each dispatch
// to at most 2^22 dab-pixel evaluations; sampled tips use at most 16 taps each.
// Pressure dynamics scale by mix(minimum, 1, pressure) ("Control: Pen Pressure" with
// Minimum Diameter/Opacity/Flow); 1 disables them. Jitters subtract up to their
// fraction (size, opacity, flow, roundness) or add up to +-jitter * 180 degrees
// (angle). They are deterministic: dab n of a stroke draws the same numbers for a
// seed. No dab exceeds diameter; a dab below 1 pixel keeps 1 pixel and scales its
// flow by its area, and a dab axis thinner than 1 pixel (low roundness) renders 1
// pixel wide with coverage scaled to its true area. Computed tips that are small or
// thin are supersampled 4x4, so hard dabs paint their geometric area at any subpixel
// position. Sampled tips (tip) use mask coverage: 1 paints, so invert dark-on-light
// artwork. The longer tip side spans the diameter; roundness and angle apply, and
// filtering averages up to 4x4 bilinear taps per pixel (pre-shrink tips needing more
// than 4x reduction). hardness applies to computed round tips only.
struct Brush {
    float diameter = 20.0f; // Size in pixels, at least 1.
    float hardness = 1.0f;  // [0, 1]; 1 keeps a one-pixel antialiased rim.
    float roundness = 1.0f; // (0, 1]: minor/major axis ratio.
    float angle = 0.0f;     // Degrees clockwise; Photoshop's Angle field is counterclockwise.
    float spacing = 0.25f;  // Fraction of diameter in [0, 10]; 0 places one dab per sample.
    const Mask* tip = nullptr;
    float minimum_size = 1.0f;       // [0, 1]
    float minimum_opacity = 1.0f;    // [0, 1]
    float minimum_flow = 1.0f;       // [0, 1]
    float size_jitter = 0.0f;        // [0, 1]
    float opacity_jitter = 0.0f;     // [0, 1]
    float flow_jitter = 0.0f;        // [0, 1]
    float angle_jitter = 0.0f;       // [0, 1]; 1 is any angle.
    float roundness_jitter = 0.0f;   // [0, 1]; moves roundness toward minimum_roundness.
    float minimum_roundness = 0.25f; // (0, 1]
    bool tilt_roundness = false;     // Roundness Control: Pen Tilt; flat pens reach the minimum.
    BrushAngleControl angle_control = BrushAngleControl::fixed;
    float scatter = 0.0f;           // [0, 10]: offset up to +-scatter * dab diameter.
    bool scatter_both_axes = false; // Otherwise scatter only across the stroke.
    std::uint32_t seed = 0;
};

// A placed dab. opacity and flow are the dynamics factors in [0, 1]; tools multiply
// them by their own opacity and flow.
struct BrushDab {
    Point center{};
    float diameter{};
    float roundness{};
    float angle{};
    float opacity{};
    float flow{};
};

// Stroke tools share the brush engine. Within one call each pixel accumulates a stroke
// coverage: dab coverage c raises it toward the dab's opacity by flow * c, and never
// above it. Opacity therefore caps the whole stroke (overlapping dabs do not build up
// past it) while flow sets how fast dabs build up, as in Photoshop. The tool is then
// applied once with that coverage. For accumulating constant-color stamps, set spacing
// to zero, color alpha to one and flow to the stamp opacity. A continuation state
// appends input events without restarting spacing, randomness or the opacity cap.
// Painting blend modes are normal through hard_light. preserve_alpha composites
// color against an opaque backdrop, then restores destination alpha; fully
// transparent pixels stay unchanged. Dodge/burn and sponge always preserve alpha.
struct BrushStrokeOptions {
    std::span<const StrokeSample> samples{};
    Brush brush{};
    Color color{};
    BlendMode mode = BlendMode::normal;
    float opacity = 1.0f;
    float flow = 1.0f;
    bool preserve_alpha = false; // Keep destination alpha, including transparent pixels.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

// Eraser tool, brush mode: scales color and alpha toward transparency.
struct EraserStrokeOptions {
    std::span<const StrokeSample> samples{};
    Brush brush{};
    float opacity = 1.0f;
    float flow = 1.0f;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

// Paint linear mask coverage toward coverage using the same dab accumulation.
// Erasing a mask paints toward zero. Selection must be distinct from destination.
struct MaskBrushStrokeOptions {
    std::span<const StrokeSample> samples{};
    Brush brush{};
    float coverage = 1.0f;
    float opacity = 1.0f;
    float flow = 1.0f;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

// Create continuation states with Context::create_brush_stroke_state or
// create_smudge_stroke_state before recording. Creation reserves a destination-sized
// snapshot, counted in memory().images or memory().masks; recording never grows it.
// Default states have no reservation and cannot record a continuation.
// Continuation states own a pre-stroke snapshot and accumulated input events.
// Pass only new samples on each call, in order; do not repeat the boundary sample.
// Each call replays the accumulated stroke from its snapshot, preserving spacing,
// random sequence, opacity and (for smudge) carried pigment exactly. This costs one
// destination-sized snapshot plus all samples, and cumulative replay work. Only the
// accumulated write bounds are captured/restored: newly covered strips are captured
// before painting, and the previous bounds are restored before replay. Snapshot
// copies touch at most the accumulated rectangle's area per call, ignoring selection
// coverage (but clipped to region and canvas). No snapshot GPU allocation occurs
// during a gesture; at 6000x4000 the reservation is 384 MB (image) or 24 MB (mask).
// CPU sample/record staging and command/data buffers can still grow. The whole
// stroke remains subject to the documented work/data limits. Keep destination size,
// tool options, tip, mask and source contents fixed, submit calls in order, and do not
// edit the destination between calls; region-limited restore no longer reverts edits
// outside the accumulated bounds. reset() starts another stroke while retaining
// snapshot capacity; its next destination must have the same size, kind and context.
// Discarding a recording containing a continuation, or a submit failure after its
// GPU work starts, invalidates that stroke. Asynchronous failure invalidates it when
// observed by wait/is_complete. Further continuation and submission of dependent
// recordings reject invalid_resource with parameter "state". Discard those recordings
// and reset() before starting a new stroke; restore the destination after failed GPU
// work if its contents must be preserved. A rejected recording operation or a submit
// failure before GPU execution preserves the existing state and recording for retry.
// Assign a default state to release storage (pending records retain it until retirement).
// States are move-only and must not be used concurrently.
class WGPUPIXEL_API BrushStrokeState {
  public:
    BrushStrokeState() = default;
    BrushStrokeState(BrushStrokeState&&) noexcept = default;
    BrushStrokeState& operator=(BrushStrokeState&&) noexcept = default;
    BrushStrokeState(const BrushStrokeState&) = delete;
    BrushStrokeState& operator=(const BrushStrokeState&) = delete;

    void reset() noexcept;

    // Conservative accumulated pre-stroke rectangle in destination pixel coordinates,
    // clipped to the canvas and region. Reflects recorded calls, not GPU completion.
    // Empty before any affected bounds, after reset, or for a discarded/failed state.
    // Pixels in the snapshot remain private; this query does not export undo data.
    [[nodiscard]] std::optional<Rect> snapshot_bounds() const noexcept;

  private:
    std::shared_ptr<detail::StrokeState> state_;
    friend class Commands;
    friend class Context;
};
class WGPUPIXEL_API SmudgeStrokeState {
  public:
    SmudgeStrokeState() = default;
    SmudgeStrokeState(SmudgeStrokeState&&) noexcept = default;
    SmudgeStrokeState& operator=(SmudgeStrokeState&&) noexcept = default;
    SmudgeStrokeState(const SmudgeStrokeState&) = delete;
    SmudgeStrokeState& operator=(const SmudgeStrokeState&) = delete;

    void reset() noexcept;

    // Same bounds and recording/completion semantics as BrushStrokeState.
    [[nodiscard]] std::optional<Rect> snapshot_bounds() const noexcept;

  private:
    std::shared_ptr<detail::StrokeState> state_;
    friend class Commands;
    friend class Context;
};

// Clone Stamp: paints source pixels found at destination position - offset (bilinear,
// transparent outside the source). Offset is destination minus the Alt-clicked source
// point. Aligned cloning keeps one offset for every stroke; non-aligned cloning
// recomputes it so each stroke's first sample maps to the source point. The source
// must be a distinct image; clone within a layer from a copy of it.
struct CloneStrokeOptions {
    std::span<const StrokeSample> samples{};
    Brush brush{};
    Point offset{};
    BlendMode mode = BlendMode::normal;
    float opacity = 1.0f;
    float flow = 1.0f;
    bool preserve_alpha = false; // Keep destination alpha, including transparent pixels.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

// Pattern Stamp: paints a pattern tiled over the image. transform maps pattern
// coordinates to image coordinates; the identity starts a tile at the image origin.
// Aligned keeps one transform; non-aligned translates it to each stroke's start.
struct PatternStrokeOptions {
    std::span<const StrokeSample> samples{};
    Brush brush{};
    Affine transform{};
    BlendMode mode = BlendMode::normal;
    float opacity = 1.0f;
    float flow = 1.0f;
    bool preserve_alpha = false; // Keep destination alpha, including transparent pixels.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

// Photoshop Dodge/Burn Range.
enum class ToneRange : std::uint32_t { shadows, midtones, highlights };

// Dodge (lighten) or burn (darken) tool. Curves act on sRGB-encoded straight color and
// scale with the stroke coverage, which exposure caps like an opacity: highlights
// multiply by 1 +- e/3, midtones apply a power of 1/(1 + e) or 1 + e/3, shadows lift
// by e/3 or cut by e/3 and rescale. Values beyond [0, 1] continue the curves: the
// sRGB encoding and midtone power mirror for negatives, the others stay linear, and
// burned shadows crush only [0, e/3) to zero. protect_tones applies the curve to
// luminance and keeps hue, pulling chroma in rather than exceeding max(1, the
// pixel's largest channel).
struct DodgeBurnStrokeOptions {
    std::span<const StrokeSample> samples{};
    Brush brush{};
    bool burn = false;
    ToneRange range = ToneRange::midtones;
    float exposure = 0.5f;
    bool protect_tones = true;
    float flow = 1.0f;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

// Sponge tool: desaturates toward linear Rec. 709 luminance, or saturates away from it,
// by the stroke coverage (flow builds it up). Saturating never makes channels negative;
// vibrance protects saturated colors and avoids clipping above one.
struct SpongeStrokeOptions {
    std::span<const StrokeSample> samples{};
    Brush brush{};
    bool saturate = false;
    float flow = 0.5f;
    bool vibrance = true;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

// Blur or Sharpen tool: mixes toward a 5x5 binomial blur, or adds the difference to it
// (unsharp masking at 100%), by the stroke coverage, which strength caps. Sharpened
// color keeps HDR overshoot and negative undershoot; where alpha leaves [0, 1] it is
// clamped and color scales with it (same straight color), and no alpha means
// transparent black. With preserve_alpha, the computed straight color is kept
// at destination alpha; a zero-alpha result leaves the destination unchanged. It reads
// pixels as they were before the call: the operation first copies the stroke area into
// a destination-sized plane in the explicitly reserved workspace.
struct FocusStrokeOptions {
    std::span<const StrokeSample> samples{};
    Brush brush{};
    bool sharpen = false;
    float strength = 0.5f;
    float flow = 1.0f;
    bool preserve_alpha = false; // Keep destination alpha, including transparent pixels.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
    Workspace workspace{};
};

// Smudge tool. Dabs run strictly in order: the first dab picks up the pixels under it
// (or color with finger_painting); each later dab mixes what it carries with the
// current pixels, carried = mix(current, carried, strength), then deposits it by dab
// coverage times dynamics opacity and flow. Pixels outside the image read as
// transparent. preserve_alpha keeps destination alpha and the deposited straight
// color; a zero-alpha deposit leaves the destination unchanged. The explicitly
// reserved workspace holds the carried patch for the duration of this call.
// Patches above 128x128 run one dab at a time in tiles of at most 1024x1024;
// smaller patches share commands of up to 2^22 patch updates. Commands grows as needed.
// Pathological strokes beyond 2^32
// patch updates (dabs times the largest patch area) fail with ErrorCode::capacity.
struct SmudgeStrokeOptions {
    std::span<const StrokeSample> samples{};
    Brush brush{};
    float strength = 0.5f;
    bool finger_painting = false;
    Color color{};
    bool preserve_alpha = false; // Keep destination alpha, including transparent pixels.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
    Workspace workspace{};
};

// Photoshop gradient tool styles. With v = p - start and d = end - start:
// linear dot(v, d) / |d|^2; radial |v| / |d|; angle sweeps clockwise on screen
// from d, one turn per ramp; reflected |linear|; diamond (|dot(v, d)| + |cross(d, v)|)
// / |d|^2, with a corner at end.
enum class GradientShape : std::uint32_t { linear, radial, angle, reflected, diamond };
// linear mixes premultiplied linear colors; perceptual mixes sRGB-encoded colors
// (Photoshop's Classic/Perceptual look). Both keep alpha interpolation premultiplied.
enum class GradientInterpolation : std::uint32_t { linear, perceptual };

// Gradient tool. stops need nondecreasing positions in [0, 1]; equal positions make a
// hard step. The ramp extends past its ends by extend: clamp (Photoshop), repeat,
// mirror, or transparent (leaves pixels outside the ramp unchanged). dither adds
// triangular noise of one 8-bit sRGB step so
// 8-bit downloads do not band. The result composites with mode and opacity, or
// replaces by opacity when replace is true (including transparent ramp colors).
struct GradientFillOptions {
    Point start{};
    Point end{};
    std::span<const GradientStop> stops{};
    GradientShape shape = GradientShape::linear;
    EdgeMode extend = EdgeMode::clamp;
    GradientInterpolation interpolation = GradientInterpolation::perceptual;
    bool reverse = false;
    bool dither = true;
    BlendMode mode = BlendMode::normal;
    float opacity = 1.0f;
    bool replace = false;        // Interpolate destination toward the ramp by opacity; ignore mode.
    bool preserve_alpha = false; // Keep destination alpha, including transparent pixels.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

// Pattern fill: tiles the pattern image, bilinear, through transform (see
// PatternStrokeOptions), and composites it with mode and opacity.
struct PatternFillOptions {
    Affine transform{};
    BlendMode mode = BlendMode::normal;
    float opacity = 1.0f;
    bool preserve_alpha = false; // Keep destination alpha, including transparent pixels.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

// Paint bucket, non-contiguous: fills every pixel whose color is within tolerance of
// the seed pixel. Colors compare as 8-bit-style sRGB-encoded channels times alpha,
// plus alpha, by their largest difference; tolerance 32/255 is Photoshop's 32.
// softness fades coverage over that much more difference. For Contiguous, pass the
// flood-fill selection from the selection tools as mask and set match_seed = false.
// tolerance and softness are normalized fractions in [0, 1].
struct PaintBucketOptions {
    Position seed{};
    float tolerance = 32.0f / 255.0f;
    float softness = 0.0f;
    Color color{};
    BlendMode mode = BlendMode::normal;
    float opacity = 1.0f;
    bool match_seed = true;      // False ignores seed, tolerance and softness entirely.
    bool preserve_alpha = false; // Keep destination alpha, including transparent pixels.
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

// Places the dabs of a stroke exactly as the stroke tools do. Writes up to dabs.size()
// dabs and returns the total count.
[[nodiscard]] WGPUPIXEL_API std::size_t brush_dabs(std::span<const StrokeSample> samples,
                                                   const Brush& brush,
                                                   std::span<BrushDab> dabs = {});
// Pixels a stroke may change, before clipping to an image; empty without samples.
[[nodiscard]] WGPUPIXEL_API std::optional<Rect> stroke_bounds(std::span<const StrokeSample> samples,
                                                              const Brush& brush);
// Workspace capacity for any stroke with these options, regardless of its samples.
// Queries do not allocate GPU memory or inspect options.workspace. Smudge patch sides
// above 65535 pixels (diameters beyond about 65000) fail with ErrorCode::capacity.
[[nodiscard]] WGPUPIXEL_API OperationRequirements
focus_stroke_requirements(ImageSize destination, const FocusStrokeOptions& options = {});
[[nodiscard]] WGPUPIXEL_API OperationRequirements
smudge_stroke_requirements(ImageSize destination, const SmudgeStrokeOptions& options = {});
// ---- end painting options ----

// ---- analysis options ----
// Measured channels, in the order of histogram counts.
enum class AnalysisChannel : std::uint32_t { red, green, blue, alpha, luminosity };
inline constexpr std::uint32_t analysis_channel_count = 5;

// Value space of measurements. Colors are always unpremultiplied first.
// srgb: sRGB-encoded values clipped to [0, 1]; the numbers Photoshop's Histogram and Info
//   panels show, and what RGBA8/RGBA16 downloads quantize.
// linear: linear light, unclipped (HDR and negative values are kept, except that
//   histogram bins clamp to [0, 1]).

// Measurements cover the region, clipped to the image, and with a mask only pixels whose
// coverage is at least 128 (at least half selected, like the marching ants of a feathered
// selection). Red, green, blue and luminosity skip pixels with zero alpha, whose color is
// undefined; alpha counts every measured pixel. Luminosity is 0.30 R + 0.59 G + 0.11 B in
// srgb (Photoshop's Luminosity channel) and Rec. 709 luminance in linear.
struct AnalysisOptions {
    ColorEncoding space = ColorEncoding::srgb;
    const Mask* mask = nullptr;
    std::optional<Rect> region{};
};

struct MaskBoundsOptions {
    // Inclusive minimum coverage in [0, 1], converted to A8 with ceil(threshold * 255).
    // Any value in (0, 1/255] measures coverage > 0; 0 includes zero coverage.
    float threshold = 1.0f / 255.0f; // Default: the smallest nonzero coverage.
    std::optional<Rect> region{}; // Pixel rectangle, clipped to the mask; empty is allowed.
};

// Zero when the channel measured no pixels. deviation is the population standard deviation.
struct ChannelStatistics {
    double minimum{}, maximum{}, mean{}, deviation{};
};

struct ImageStatistics {
    std::uint64_t pixels{};       // Measured pixels; alpha statistics cover these.
    std::uint64_t color_pixels{}; // Measured pixels with alpha > 0: red, green, blue, luminosity.
    ChannelStatistics red, green, blue, alpha, luminosity;
    // Mean linear premultiplied color of all measured pixels, independent of space:
    // the eyedropper's sampled color, directly usable as an operation color.
    Color average{};
};

// Photoshop's eyedropper Sample Size: the size x size square centered on the pixel under
// point. 1 is Point Sample; 3, 5, 11, 31, 51 and 101 are the N by N Average presets.
// Measure it with AnalysisOptions::region and read ImageStatistics::average; parts
// outside the image are ignored. size must be odd and positive.
[[nodiscard]] WGPUPIXEL_API Rect sample_region(Point point, std::int32_t size = 1);
// ---- end analysis options ----

// Copyable handles share mutable storage; const handles still permit GPU writes.
// Resources are released when their last handle, recording or pending submission
// releases ownership. destroy() explicitly invalidates all aliases and rejects busy
// resources. Default/moved-from handles are empty. Newly allocated images are
// transparent black, masks are zero. set_size changes row layout within capacity
// without clearing or resampling; recorded and pending work keep the dimensions they
// captured, so later commands see the new size. All aliases see it.
// revision starts at zero and advances on a changed size and on each submitted
// write, before GPU completion. It is a cache key, not a completion signal.
class WGPUPIXEL_API Image {
  public:
    Image() noexcept = default;
    [[nodiscard]] ImageSize size() const;
    [[nodiscard]] std::uint64_t capacity_pixels() const;
    [[nodiscard]] std::uint64_t revision() const;
    void set_size(ImageSize size);

  private:
    std::shared_ptr<detail::Resource> resource_;
    explicit Image(std::shared_ptr<detail::Resource> resource);
    friend class Context;
    friend class Commands;
    friend class webgpu::Presenter;
};

// An 8-bit linear coverage plane. Shares the Image lifetime, sizing and revision contract.
class WGPUPIXEL_API Mask {
  public:
    Mask() noexcept = default;
    [[nodiscard]] ImageSize size() const;
    [[nodiscard]] std::uint64_t capacity_pixels() const;
    [[nodiscard]] std::uint64_t revision() const;
    void set_size(ImageSize size);

  private:
    std::shared_ptr<detail::Resource> resource_;
    explicit Mask(std::shared_ptr<detail::Resource> resource);
    friend class Context;
    friend class Commands;
    friend class webgpu::Presenter;
};

class WGPUPIXEL_API UploadBuffer {
  public:
    UploadBuffer() noexcept = default;
    [[nodiscard]] std::uint64_t capacity_pixels() const;
    [[nodiscard]] std::uint32_t bytes_per_pixel() const;

  private:
    std::shared_ptr<detail::Resource> resource_;
    explicit UploadBuffer(std::shared_ptr<detail::Resource> resource);
    friend class Context;
    friend class Commands;
};

class WGPUPIXEL_API ReadbackBuffer {
  public:
    ReadbackBuffer() noexcept = default;
    [[nodiscard]] std::uint64_t capacity_pixels() const;
    [[nodiscard]] std::uint32_t bytes_per_pixel() const;

  private:
    std::shared_ptr<detail::Resource> resource_;
    explicit ReadbackBuffer(std::shared_ptr<detail::Resource> resource);
    friend class Context;
    friend class Commands;
};

// ---- analysis resources ----
// Per-channel histogram on the GPU: analysis_channel_count rows of bins() 32-bit counts,
// in AnalysisChannel order. Bin k counts values v with round(v * (bins - 1)) == k, so 256
// bins hold exactly the 8-bit levels of an RGBA8 download. Read after the submission
// completes; like transfer buffers, it is busy while recorded or pending.
class WGPUPIXEL_API HistogramBuffer {
  public:
    HistogramBuffer() noexcept = default;
    [[nodiscard]] std::uint32_t bins() const;

  private:
    std::shared_ptr<detail::Resource> resource_;
    explicit HistogramBuffer(std::shared_ptr<detail::Resource> resource);
    friend class Context;
    friend class Commands;
};

// Holds GPU partial moments of one statistics command; Context::read reduces them in
// double precision. Its size does not depend on the image.
class WGPUPIXEL_API StatisticsBuffer {
  public:
    StatisticsBuffer() noexcept = default;

  private:
    std::shared_ptr<detail::Resource> resource_;
    explicit StatisticsBuffer(std::shared_ptr<detail::Resource> resource);
    friend class Context;
    friend class Commands;
};

// Exact mask bounds: 16 bytes on the GPU plus 16 bytes for readback, independent
// of mask size. Shared lifetime and recorded/pending busy rules as HistogramBuffer.
class WGPUPIXEL_API MaskBoundsBuffer {
  public:
    MaskBoundsBuffer() noexcept = default;

  private:
    std::shared_ptr<detail::Resource> resource_;
    explicit MaskBoundsBuffer(std::shared_ptr<detail::Resource> resource);
    friend class Context;
    friend class Commands;
};
// ---- end analysis resources ----

// An ordered recording, reusable after its submission retires via wait/is_complete.
// Every operation that throws leaves earlier commands, staged data and temporary
// resources intact and discards everything recorded by that operation. No GPU work
// executes until submit. Recording does not change resource revisions.
// Capacity grows automatically; growth obeys the context memory limit and device
// buffer limits. Temporary resources remain alive until submission retirement.
// Resources must belong to this context. Separate source/destination arguments
// must not alias unless an operation explicitly allows it; in-place operations
// expose a single destination. Workspace contents are unspecified afterwards.
// A mask blends each result with the destination's previous pixel by coverage;
// it is destination-sized. Regions clip writes to destination bounds, leaving other
// pixels unchanged. Neighborhood reads may extend outside the region and selection.
class WGPUPIXEL_API Commands {
  public:
    Commands() noexcept;
    ~Commands();
    Commands(Commands&&) noexcept;
    Commands& operator=(Commands&&) noexcept;
    Commands(const Commands&) = delete;
    Commands& operator=(const Commands&) = delete;

    void upload(const UploadBuffer& source, const Mask& destination,
                const TransferOptions& options = {});
    void download(const Mask& source, const ReadbackBuffer& destination,
                  const TransferOptions& options = {});
    void fill(const Mask& mask, const MaskFillOptions& options);
    void copy(const Mask& source, const Mask& destination, const CopyOptions& options = {});
    void invert(const Mask& destination, const InvertOptions& options = {});
    void upload(const UploadBuffer& source, const Image& destination,
                const TransferOptions& options = {});
    void download(const Image& source, const ReadbackBuffer& destination,
                  const TransferOptions& options = {});
    // Copy equal-sized images; no color conversion.
    void copy(const Image& source, const Image& destination, const CopyOptions& options = {});
    // Replace pixels with a linear premultiplied color, blended by mask coverage.
    void fill(const Image& destination, const FillOptions& options);

    // ---- generation commands ----
    void polygon(const Image& destination, const PolygonOptions& options);
    void perlin(const Image& destination, const PerlinOptions& options);
    void noise(const Image& destination, const NoiseOptions& options = {});
    void circle(const Image& destination, const CircleOptions& options);
    void dots(const Image& destination, const DotsOptions& options);
    void stripes(const Image& destination, const StripesOptions& options);
    void grid(const Image& destination, const GridOptions& options);
    void checkerboard(const Image& destination, const CheckerboardOptions& options);
    // ---- end generation commands ----

    // ---- compositing commands ----
    void apply_mask(const Mask& source, const Image& destination,
                    const ApplyMaskOptions& options = {});
    // Scale all premultiplied channels by factor in [0, 1].
    void opacity(const Image& destination, const OpacityOptions& options);
    void blend_many(std::span<const Image> sources, const Image& destination,
                    const BlendManyOptions& options);
    void blend(const Image& source, const Image& destination, const BlendOptions& options);
    // ---- end compositing commands ----

    // ---- geometry commands ----
    void zoom(const Image& source, const Image& destination, const ZoomOptions& options);
    void crop(const Image& source, const Image& destination, const CropOptions& options);
    void rotate(const Image& source, const Image& destination, const RotateOptions& options);
    void flip(const Image& source, const Image& destination, const FlipOptions& options = {});
    void resize(const Image& source, const Image& destination, const ResizeOptions& options = {});
    void transform(const Image& source, const Image& destination, const TransformOptions& options);
    void transform(const Image& source, const Image& destination,
                   const ProjectiveTransformOptions& options);
    void perspective(const Image& source, const Image& destination,
                     const PerspectiveOptions& options);
    void offset(const Image& source, const Image& destination, const OffsetOptions& options);
    // Geometry for masks, so layer masks follow their layers. Coverage is filtered like
    // an alpha channel and rounded to bytes; nearest, crop, flip and offset are exact.
    // mask selects which destination coverage changes and must differ from destination.
    void transform(const Mask& source, const Mask& destination, const TransformOptions& options);
    void transform(const Mask& source, const Mask& destination,
                   const ProjectiveTransformOptions& options);
    void perspective(const Mask& source, const Mask& destination,
                     const PerspectiveOptions& options);
    void offset(const Mask& source, const Mask& destination, const OffsetOptions& options);
    void resize(const Mask& source, const Mask& destination, const ResizeOptions& options = {});
    void crop(const Mask& source, const Mask& destination, const CropOptions& options);
    void flip(const Mask& source, const Mask& destination, const FlipOptions& options = {});
    void rotate(const Mask& source, const Mask& destination, const RotateOptions& options);
    void zoom(const Mask& source, const Mask& destination, const ZoomOptions& options);
    // ---- end geometry commands ----

    // ---- adjustments commands ----
    void chroma_key(const Image& destination, const ChromaKeyOptions& options);
    // Invert each straight linear channel strictly above value in [0, 1].
    void solarize(const Image& destination, const SolarizeOptions& options = {});
    void threshold(const Image& destination, const ThresholdOptions& options = {});
    // Replace straight linear RGB with 1 - RGB; preserve alpha, without clipping.
    void invert(const Image& destination, const InvertOptions& options = {});
    void sepia(const Image& destination, const SepiaOptions& options = {});
    void vibrance(const Image& destination, const VibranceOptions& options);
    // HSV hue rotation of linear RGB; finite degrees wrap modulo 360. Preserve alpha.
    void hue(const Image& destination, const HueOptions& options);
    // Signed power of straight linear RGB: sign(x) * abs(x)^(1/value).
    // value must be finite and positive with finite reciprocal; alpha is preserved.
    void gamma(const Image& destination, const GammaOptions& options);
    // Mix linear Rec.709 gray toward RGB by finite nonnegative factor.
    void saturation(const Image& destination, const SaturationOptions& options);
    // Scale straight linear RGB about 0.5 by finite nonnegative factor.
    void contrast(const Image& destination, const ContrastOptions& options);
    // Multiply linear RGB by 2^stops, which must be normal finite float32.
    void exposure(const Image& destination, const ExposureOptions& options);
    // Add finite amount to straight linear RGB; preserve alpha and retain HDR.
    void brightness(const Image& destination, const BrightnessOptions& options);
    // Linear Rec.709 luminance replicated into RGB; preserve alpha.
    void grayscale(const Image& destination, const GrayscaleOptions& options = {});
    void levels(const Image& destination, const LevelsOptions& options);
    void curves(const Image& destination, const CurvesOptions& options);
    void lut(const Image& destination, const LutOptions& options);
    void lut3d(const Image& destination, const Lut3DOptions& options);
    void color_balance(const Image& destination, const ColorBalanceOptions& options);
    void hue_saturation(const Image& destination, const HueSaturationOptions& options);
    void color_matrix(const Image& destination, const ColorMatrixOptions& options);
    void channel_mixer(const Image& destination, const ChannelMixerOptions& options);
    void black_white(const Image& destination, const BlackWhiteOptions& options);
    void photo_filter(const Image& destination, const PhotoFilterOptions& options);
    void posterize(const Image& destination, const PosterizeOptions& options);
    void gradient_map(const Image& destination, const GradientMapOptions& options);
    // ---- end adjustments commands ----

    // ---- filters commands ----
    void vignette(const Image& destination, const VignetteOptions& options);
    void drop_shadow(const Image& source, const Image& destination,
                     const DropShadowOptions& options);
    void rounded_corners(const Image& destination, const RoundedCornersOptions& options);
    void emboss(const Image& source, const Image& destination, const EmbossOptions& options = {});
    // Equal-sized, distinct images. 3x3 Sobel magnitude of straight linear Rec.709
    // luminance, clamped at image edges; preserve center alpha, without RGB clipping.
    void sobel(const Image& source, const Image& destination, const SobelOptions& options = {});
    // Equal-sized, distinct images. Four-neighbor Laplacian on premultiplied RGB,
    // clamped at image edges; preserve center alpha. Nonnegative strength must
    // give a finite center weight 1 + 4 * strength. No RGB clipping.
    void sharpen(const Image& source, const Image& destination, const SharpenOptions& options = {});
    void gaussian_blur(const Image& destination, const GaussianBlurOptions& options);
    void box_blur(const Image& source, const Image& destination,
                  const BoxBlurOptions& options = {});
    void motion_blur(const Image& source, const Image& destination,
                     const MotionBlurOptions& options = {});
    void radial_blur(const Image& source, const Image& destination,
                     const RadialBlurOptions& options = {});
    void unsharp_mask(const Image& source, const Image& destination,
                      const UnsharpMaskOptions& options = {});
    void high_pass(const Image& source, const Image& destination,
                   const HighPassOptions& options = {});
    void median(const Image& source, const Image& destination, const MedianOptions& options = {});
    void minimum(const Image& source, const Image& destination,
                 const MorphologyOptions& options = {});
    void maximum(const Image& source, const Image& destination,
                 const MorphologyOptions& options = {});
    void pixelate(const Image& source, const Image& destination, const PixelateOptions& options = {});
    void surface_blur(const Image& source, const Image& destination,
                      const SurfaceBlurOptions& options = {});
    void add_noise(const Image& destination, const AddNoiseOptions& options = {});
    // ---- end filters commands ----

    // ---- selection commands ----
    void extract_mask(const Image& source, const Mask& destination,
                      const ExtractMaskOptions& options = {});
    void select_rectangle(const Mask& destination, const RectangleSelectionOptions& options);
    void select_ellipse(const Mask& destination, const EllipseSelectionOptions& options);
    void select_polygon(const Mask& destination, const PolygonSelectionOptions& options);
    void combine(const Mask& source, const Mask& destination,
                 const MaskCombineOptions& options = {});
    void feather(const Mask& mask, const FeatherOptions& options);
    void expand(const Mask& mask, const ExpandOptions& options);
    void contract(const Mask& mask, const ContractOptions& options);
    void border(const Mask& mask, const BorderOptions& options);
    void smooth(const Mask& mask, const SmoothOptions& options);
    void threshold(const Mask& mask, const MaskThresholdOptions& options = {});
    void levels(const Mask& mask, const MaskLevelsOptions& options);
    void select_color_range(const Image& source, const Mask& destination,
                            const ColorRangeOptions& options);
    // Exact in one submission: tile-local union-find in workgroup memory, then global
    // lock-free union-find across tile borders; no CPU readback or iteration count.
    // Five dispatches (three when not contiguous), O(pixels) work.
    void select_magic_wand(const Image& source, const Mask& destination,
                           const MagicWandOptions& options);
    // ---- end selection commands ----

    // ---- painting commands ----
    void brush_stroke(const Image& destination, const BrushStrokeOptions& options);
    void eraser_stroke(const Image& destination, const EraserStrokeOptions& options);
    void clone_stroke(const Image& source, const Image& destination,
                      const CloneStrokeOptions& options);
    void pattern_stroke(const Image& pattern, const Image& destination,
                        const PatternStrokeOptions& options);
    void dodge_burn_stroke(const Image& destination, const DodgeBurnStrokeOptions& options);
    void sponge_stroke(const Image& destination, const SpongeStrokeOptions& options);
    void focus_stroke(const Image& destination, const FocusStrokeOptions& options);
    void smudge_stroke(const Image& destination, const SmudgeStrokeOptions& options);
    void gradient_fill(const Image& destination, const GradientFillOptions& options);
    void pattern_fill(const Image& pattern, const Image& destination,
                      const PatternFillOptions& options);
    void paint_bucket(const Image& destination, const PaintBucketOptions& options);
    void brush_stroke(const Mask& destination, const MaskBrushStrokeOptions& options);
    void eraser_stroke(const Mask& destination, const EraserStrokeOptions& options);
    void brush_stroke(const Image& destination, BrushStrokeState& state,
                      const BrushStrokeOptions& options);
    void eraser_stroke(const Image& destination, BrushStrokeState& state,
                       const EraserStrokeOptions& options);
    void brush_stroke(const Mask& destination, BrushStrokeState& state,
                      const MaskBrushStrokeOptions& options);
    void eraser_stroke(const Mask& destination, BrushStrokeState& state,
                       const EraserStrokeOptions& options);
    void smudge_stroke(const Image& destination, SmudgeStrokeState& state,
                       const SmudgeStrokeOptions& options);
    // ---- end painting commands ----

    // ---- analysis commands ----
    // Replace the buffer's contents with measurements of source (see AnalysisOptions).
    void histogram(const Image& source, const HistogramBuffer& destination,
                   const AnalysisOptions& options = {});
    void statistics(const Image& source, const StatisticsBuffer& destination,
                    const AnalysisOptions& options = {});
    // Replace the result with the bounds of texels meeting threshold inside region.
    // O(region pixels), at most 2^20 texels per dispatch; no workspace required.
    void mask_bounds(const Mask& source, const MaskBoundsBuffer& destination,
                     const MaskBoundsOptions& options = {});
    // ---- end analysis commands ----

  private:
    void
    continue_stroke(const std::shared_ptr<detail::Resource>& destination,
                    std::shared_ptr<detail::StrokeState>& state,
                    std::span<const StrokeSample> samples, std::string_view tool,
                    const std::function<void(Commands&, std::span<const StrokeSample>)>& record);
    std::unique_ptr<detail::Recording> recording_;
    explicit Commands(std::unique_ptr<detail::Recording> recording);
    friend class Context;
};

// Copyable completion token tied to its originating context. Default tokens are
// invalid. Destroying a token does not cancel work or release pending resources.
class WGPUPIXEL_API Submission {
  public:
    Submission() noexcept = default;

  private:
    std::shared_ptr<detail::Flight> flight_;
    explicit Submission(std::shared_ptr<detail::Flight> flight);
    friend class Context;
    friend class webgpu::Presenter;
};

// Requested GPU allocation bytes, including buffer alignment and all owned texture
// texels (not driver overhead). Borrowed native resources are excluded. Internal
// includes uniforms/data, analysis results and one 16-byte placeholder binding made
// by the first mask geometry call. Presentation counts owned display textures and
// explicitly reserved viewport caches. Transfers include both readback staging buffers.
// Workspace counts every buffer explicitly allocated by create_workspace.
struct MemoryUsage {
    std::uint64_t images = 0, masks = 0, transfers = 0, internal = 0, presentation = 0;
    std::uint64_t workspace = 0;
    std::uint64_t total = 0, peak = 0;
};

// Per-device ceilings, independent of the current memory budget/usage. Image and
// mask dimensions must each fit max_image_dimension and their pixel product must
// fit the corresponding pixel limit. These bounds do not promise free memory.
struct ResourceLimits {
    std::uint64_t max_buffer_bytes;
    std::uint64_t max_storage_binding_bytes;
    std::uint64_t max_image_pixels, max_mask_pixels;
    std::uint32_t max_image_dimension;
    // Per-record payload and total aligned staged payload in one Commands batch.
    std::uint64_t max_staged_data_bytes, max_recorded_data_bytes;
    std::uint32_t staged_data_alignment;
    // The adapter's own 2D texture limit (often 16384; WebGPU's default is 8192):
    // bounds Display and Presenter target width and height.
    std::uint32_t max_texture_dimension_2d;
};

// Unless stated otherwise, externally serialize calls using the same Context and its
// associated objects, including command recorders and stroke continuation states.
// memory() and limits() may be queried concurrently. Do not move, assign or destroy a
// handle object concurrently with access to that same object. Calls may move between
// threads when externally synchronized; presentation also follows the host platform's
// surface rules.
class WGPUPIXEL_API Context {
  public:
    Context() noexcept = default;
    ~Context();
    Context(Context&&) noexcept;
    Context& operator=(Context&&) noexcept;
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    // Opens the device without compiling shaders. Each processing pipeline compiles
    // the first time a submission uses it, before that submission reaches the GPU;
    // compilation failures throw from submit.
    [[nodiscard]] static Context create();
    // Compiles every processing pipeline now, for applications that prefer to pay
    // the cost up front (for example at startup). Idempotent.
    void prepare();
    // Thread-safe snapshots; resources retained by pending submissions remain counted.
    [[nodiscard]] MemoryUsage memory() const;
    [[nodiscard]] ResourceLimits limits() const;
    // 0 = unlimited. Lowering below current usage does not evict resources. Further
    // allocations exceeding the limit throw capacity before calling the driver,
    // naming the allocating operation and parameter "memory_limit".
    void set_memory_limit(std::uint64_t bytes);
    // Start a new peak interval at current usage.
    void reset_peak();
    // Reserve persistent snapshot storage; the destination is not read until the
    // first continuation call. reset() reuses this storage for another stroke.
    [[nodiscard]] BrushStrokeState create_brush_stroke_state(const Image& destination);
    [[nodiscard]] BrushStrokeState create_brush_stroke_state(const Mask& destination);
    [[nodiscard]] SmudgeStrokeState create_smudge_stroke_state(const Image& destination);

    // Allocate exactly the plan's aligned buffer capacities, subject to device
    // limits and the memory budget. A larger capacity per slot is always valid.
    [[nodiscard]] Workspace create_workspace(const WorkspacePlan& plan);
    void destroy(const Workspace& workspace);
    [[nodiscard]] Mask create_mask(ImageSize size);
    [[nodiscard]] UploadBuffer create_upload_buffer(const Mask& mask,
                                                    const MaskTransferBufferOptions& options = {});
    [[nodiscard]] ReadbackBuffer
    create_readback_buffer(const Mask& mask, const MaskTransferBufferOptions& options = {});
    // Explicitly invalidate every alias and free storage. Recorded/pending uses
    // reject destruction with resource_busy; wait or poll their submissions first.
    void destroy(const Mask& mask);
    [[nodiscard]] Image create_image(ImageSize size);
    [[nodiscard]] UploadBuffer create_upload_buffer(const Image& image,
                                                    const TransferBufferOptions& options = {});
    [[nodiscard]] ReadbackBuffer create_readback_buffer(const Image& image,
                                                        const TransferBufferOptions& options = {});
    // Initial allocation hint, rounded up to one; recording grows automatically.
    [[nodiscard]] Commands create_commands(std::size_t command_capacity = 256);
    // Copy tightly packed CPU pixels into staging. Recorded/pending uses are busy.
    void write(const UploadBuffer& buffer, std::span<const std::uint8_t> pixels);
    // Read the complete most recent download payload after successful retirement.
    void read(const ReadbackBuffer& buffer, std::span<std::uint8_t> pixels);
    // Queue this recording and retain its resources. Queue order preserves dependencies
    // between submissions; CPU read/write/destroy require retirement.
    // Large recordings may send and wait for internal batches before returning.
    // A failure before the first GPU batch preserves the recording for retry. Once
    // internal batches have started, a submit failure discards the entire recording
    // and leaves Commands empty and reusable: some destination writes may have run.
    // Failed submissions also leave output invalid. Restore affected destinations
    // before retrying the work; GPU writes are not rolled back. Continuation states
    // used by discarded or failed work require reset() before starting a new stroke.
    [[nodiscard]] Submission submit(Commands& commands);
    void wait(const Submission& submission);
    // Poll without waiting; retire completed resources. Failed submissions throw.
    // Browser callers must yield to the event loop between polls.
    [[nodiscard]] bool is_complete(const Submission& submission);
    void submit_and_wait(Commands& commands) {
        wait(submit(commands));
    }
    // Record once, submit, then wait. If recording throws, this helper does not
    // submit the batch. Other callback side effects are not rolled back.
    // Discarding that recording invalidates any continuation states used in it.
    // Record only: do not move/submit commands or replace the context inside record.
    template <class Function>
        requires std::invocable<Function, Commands&> &&
                 std::same_as<std::invoke_result_t<Function, Commands&>, void>
    void run_and_wait(Function&& record) {
        auto commands = create_commands();
        std::invoke(std::forward<Function>(record), commands);
        submit_and_wait(commands);
    }
    // Same shared-handle invalidation and busy contract as destroy(Mask).
    void destroy(const Image& image);
    void destroy(const UploadBuffer& buffer);
    void destroy(const ReadbackBuffer& buffer);
    // ---- analysis context ----
    // bins lies in [2, 4096]; 256 matches 8-bit levels.
    [[nodiscard]] HistogramBuffer create_histogram_buffer(std::uint32_t bins = 256);
    [[nodiscard]] StatisticsBuffer create_statistics_buffer();
    [[nodiscard]] MaskBoundsBuffer create_mask_bounds_buffer();
    // counts must hold analysis_channel_count * bins() values.
    void read(const HistogramBuffer& buffer, std::span<std::uint32_t> counts);
    [[nodiscard]] ImageStatistics read(const StatisticsBuffer& buffer);
    // After successful retirement: half-open bounds in source-mask pixel coordinates,
    // or nullopt if no texels qualify. An unmeasured buffer throws, as for statistics.
    [[nodiscard]] std::optional<Rect> read(const MaskBoundsBuffer& buffer);
    void destroy(const HistogramBuffer& buffer);
    void destroy(const StatisticsBuffer& buffer);
    void destroy(const MaskBoundsBuffer& buffer);
    // ---- end analysis context ----

  private:
    std::shared_ptr<detail::State> state_;
    explicit Context(std::shared_ptr<detail::State> state);
    friend struct detail::Presentation;
    friend class webgpu::Presenter;
    friend class webgpu::Display;
};
} // namespace wgpupixel
