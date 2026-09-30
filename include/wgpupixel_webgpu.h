#pragma once

#include "wgpupixel.h"
#include <webgpu/webgpu.h>

namespace wgpupixel::webgpu {

// Borrowed handles. Keep Context alive; never destroy these handles. Submit all
// external rendering through this device/queue and serialize access with core calls.
struct NativeContext {
    WGPUInstance instance;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
};
[[nodiscard]] WGPUPIXEL_API NativeContext native_context(const Context&);

// Editor canvas view for Presenter::draw and Display::draw. Colors are linear
// premultiplied like image pixels; opaque background and checker colors give an opaque view.
struct ViewportOptions {
    // Maps image coordinates to target pixels (continuous coordinates, see Point). Zoom z
    // with the image origin at target (x, y) is Affine::translate(x, y) * Affine::scale(z);
    // rotation (Photoshop's Rotate View) and flips are allowed. Must be invertible.
    // Zoom of 100% and more shows nearest image pixels as crisp squares. Zooming out
    // box-filters each target pixel's footprint, per axis, over a cached float32 pyramid
    // (about a third of the image's storage) whose texels are exact area means, so fine
    // detail does not alias and HDR averages are kept. Unequal x/y zoom of any ratio reduces
    // the pyramid level further along the longer axis (cached, at most half the image,
    // rounded up along the reduced axis), so the other axis keeps its detail. Each target
    // pixel reads at most
    // 5 x 5 texels. Power-of-two zooms average exact pixel blocks; other zooms treat texels
    // cut by the box edge as uniform, a small approximation (a few 8-bit levels on 1-pixel
    // checks). Rotated views filter the footprint's axis-aligned bounding box, slightly
    // softer than the true footprint.
    Affine view{};
    // Pasteboard outside the canvas; the default is sRGB gray 40.
    Color background{0.0212f, 0.0212f, 0.0212f, 1};
    // Transparency grid behind the image, in target pixels and anchored at the canvas
    // origin rounded to whole target pixels. Like Photoshop's, it neither zooms nor rotates;
    // the default is its Medium white and sRGB gray 204 grid. Zero size shows checker_first.
    std::uint32_t checker_size = 8;
    Color checker_first{1, 1, 1, 1};
    Color checker_second{0.6038f, 0.6038f, 0.6038f, 1};
    // Quick Mask style selection overlay: an image-sized mask whose unselected (masked)
    // areas are tinted with overlay_color, Photoshop's "Color Indicates: Masked Areas"
    // with red at 50% opacity. overlay_selected tints selected areas instead.
    const Mask* overlay = nullptr;
    Color overlay_color{0.5f, 0, 0, 0.5f};
    bool overlay_selected = false;
    // One-pixel lines between image pixels once zoom exceeds pixel_grid_zoom (Photoshop
    // shows its pixel grid above 500%).
    bool pixel_grid = false;
    Color pixel_grid_color{0.175f, 0.175f, 0.175f, 0.35f};
    float pixel_grid_zoom = 5;
};

// Per-draw bounds of ALL pixel edits after since_revision, in source-image coordinates.
// Partial updates are only an optimisation: a cache whose revision, resource, size
// history or generation does not match rebuilds fully. Sharing a hint across views,
// skipping draws, or retrying after a failed draw is safe.
// The caller must include every edit since since_revision; omitted edits cannot be
// detected. Omit the hint when complete bounds are unknown. Bounds never limit the
// target draw; image and overlay hints are independent.
struct DirtyHint {
    // Half-open bounds, clipped to the source. Nonnegative width/height;
    // an empty rectangle asserts no pixels changed since since_revision.
    Rect region{};
    // Image::revision() or Mask::revision() captured BEFORE the described edits.
    std::uint64_t since_revision = 0;
};

// Large persistent cache storage needed for a viewport. Small uniforms and the
// fixed empty binding are excluded. The query reads no resource handles; overlay
// presence selects mask storage, while its dimensions/context are checked on draw.
struct ViewportRequirements {
    std::uint64_t image_pyramid = 0, mask_pyramid = 0;
    std::uint64_t image_axis = 0, mask_axis = 0;
    std::uint64_t total = 0;
};
[[nodiscard]] WGPUPIXEL_API ViewportRequirements
viewport_requirements(ImageSize source, const ViewportOptions& options);

// Direct GPU display of linear premultiplied float32 images, with bilinear scaling.
// Targets must belong to this device, match the selected format and dimensions,
// and have RenderAttachment usage. Retain target resources until submission completes.
class WGPUPIXEL_API Presenter {
public:
    Presenter() noexcept;
    ~Presenter();
    Presenter(Presenter&&) noexcept;
    Presenter& operator=(Presenter&&) noexcept;
    Presenter(const Presenter&) = delete;
    Presenter& operator=(const Presenter&) = delete;

    [[nodiscard]] static Presenter create(Context&, WGPUTextureFormat);
    [[nodiscard]] Submission draw(const Image&, WGPUTextureView target,
                                  std::uint32_t width, std::uint32_t height);
    // Explicitly reserve caches for this size and view. Capacities only grow;
    // reserve several anticipated views to cover their largest needs per cache.
    // Cache buffers count as memory().presentation. Existing submissions retain old
    // buffers if a reservation replaces them. draw never grows these caches.
    void reserve(ImageSize source, const ViewportOptions& options);
    // Draws a viewport. The pyramid for zoomed-out views is cached per Presenter and
    // updated when the image or overlay revision changes; matching dirty hints
    // limit that work. Zoom above 25% usually needs none.
    // overlay_dirty_hint is ignored when options.overlay is null.
    // A missing or insufficient reservation throws capacity before submission.
    [[nodiscard]] Submission draw(const Image&, WGPUTextureView target, std::uint32_t width,
                                  std::uint32_t height, const ViewportOptions& options);
    [[nodiscard]] Submission draw(const Image&, WGPUTextureView target, std::uint32_t width,
                                  std::uint32_t height, const ViewportOptions& options,
                                  std::optional<DirtyHint> dirty_hint,
                                  std::optional<DirtyHint> overlay_dirty_hint = {});
private:
    std::unique_ptr<detail::Presentation> state_;
    explicit Presenter(std::unique_ptr<detail::Presentation>);
};

// An owned RGBA8 presentation texture. Keeps its GPU context alive.
// The GUI borrows view(); unregister it before closing or destroying the display.
// draw() returns a completion token for Context::wait/is_complete. wait() completes
// the last draw. Keep borrowed views registered only while the display is open.
class WGPUPIXEL_API Display {
public:
    Display() noexcept;
    ~Display();
    Display(Display&&) noexcept;
    Display& operator=(Display&&) noexcept;
    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;

    [[nodiscard]] static Display create(Context&, std::uint32_t width, std::uint32_t height);
    void reserve(ImageSize source, const ViewportOptions& options);
    Submission draw(const Image&);
    Submission draw(const Image&, const ViewportOptions& options);
    Submission draw(const Image&, const ViewportOptions& options,
                    std::optional<DirtyHint> dirty_hint,
                    std::optional<DirtyHint> overlay_dirty_hint = {});
    void wait();
    void close();
    [[nodiscard]] WGPUTextureView view() const;
private:
    std::unique_ptr<detail::DisplayState> state_;
    explicit Display(std::unique_ptr<detail::DisplayState>);
};

} // namespace wgpupixel::webgpu
