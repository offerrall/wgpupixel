# Changelog

## Unreleased

- `from_srgb` and `to_srgb` convert straight sRGB UI colours to and from linear
  premultiplied `Color`, with the float32 curve of the `rgba8`/`rgba16` transfers.
- The device requests the adapter's `maxTextureDimension2D`, so `Display` and
  `Presenter` targets above 8192 px (multi-monitor spans, large offscreen targets)
  work where the GPU allows them; `Context::limits().max_texture_dimension_2d`
  reports that value.

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
