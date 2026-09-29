# Changelog

## Unreleased

- Image loading preserves 16-bit PNG/TIFF precision and half/float EXR and float
  TIFF HDR/negative samples. 16-bit ICC conversion now produces linear float32,
  preserving matrix/shaper colors near and outside the sRGB gamut; large ICC
  conversions use up to four workers.
- Float ICC input rejects table TRCs and LUT profiles instead of silently clipping
  or quantizing. Supported parametric matrix/shaper profiles use floating arithmetic;
  ICC samples outside the finite magnitude limit of `1e18` are rejected.
- Untagged 8-bit grayscale TIFF now uses sRGB instead of gamma 2.2. Associated-alpha
  TIFF, including 8-bit input, is unpremultiplied before nonlinear color conversion
  and premultiplied in linear light. Linear associated float ICC input preserves color at
  zero alpha without an unpremultiply/repremultiply round trip.
- Optional TIFF EXIF parse failures no longer prevent decoded images from loading.
  Float normalization and orientation use fewer simultaneous full-image buffers.
- Added exact GPU mask bounds and emptiness queries with normalized coverage thresholds,
  clipped regions, bounded dispatches, and a 16-byte result read through the existing
  analysis-buffer API.
- Mask bounds queries copy the 16-byte result to staging only after the final dispatch.

## 1.0.2 - 2026-09-29

- Every test suite has a 10-minute timeout, so the slower GPU suites pass on
  software Vulkan in CI and the release workflow publishes the prebuilt SDK,
  which 1.0.1 did not.

## 1.0.1 - 2026-09-29

- Prebuilt Linux x86_64 SDK attached to every GitHub release, built and tested by
  the new release workflow.
- Documented consumption with `FetchContent`, verified from a separate project.
- The version lives in `project()` of `CMakeLists.txt`; the `VERSION` file is gone.
- Tests pass on software Vulkan (lavapipe): the affine test is compiled without
  floating-point contraction, the filter contract test fits the device's buffer
  limit, and the large-filter test has a longer timeout.

## 1.0.0 - 2026-09-29

C++23/WebGPU image primitives with linear premultiplied float32 RGBA images,
HDR and negative color support, and A8 selection masks.

- Commands grow automatically from an initial capacity hint. Recording failures
  preserve earlier work. Each processing pipeline compiles on its first
  submission; `Context::prepare()` compiles them all up front. `Context::limits()` reports resource limits,
  `Context::is_complete()` polls submissions, and `Display::draw()` returns one.
- Large GPU working storage is reserved before recording. Filters and effects,
  geometry reductions, selection edits, focus and smudge take a caller-created
  `Workspace` sized by their `*_requirements` query; plans merge for reuse.
  Stroke continuation states and viewport caches are reserved explicitly by their
  owners. Resources are released when their last owner lets them go.
- Filters include exact local float medians, bounded box and round morphology
  dispatches, and bounded native submission storage.
- Painting provides image and mask brushes/erasers, stroke continuation states,
  alpha preservation, gradient replacement through `gradient_fill.replace`, and
  selection-only bucket filling through `paint_bucket.match_seed = false`.
- Mask edits and geometry support write masks and regions. A8 transfers support
  partial regions and configurable buffer capacity; `apply_mask` applies coverage
  to image pixels. `select_color_range` and `select_magic_wand` use normalized
  color-distance controls.
- Image and mask levels share `LevelsTransfer`; color controls share
  `ColorEncoding`, and measurements share `AnalysisOptions`. Geometry includes
  fractional `Point` zoom centers and affine/projective `transform` overloads.
- The public header and generated reference group the API by area. Rendered
  examples and native tests cover the public API.
