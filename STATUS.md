# Verification status

Audit ledger for each area of the library: what was verified, how, which tests
guard it, and what is known to be limited or open. Read [AGENTS.md](AGENTS.md)
first. Limits and approximations are documented in the public headers; this file
only points to them.

The 2026-09-27 review found additional import-precision, extreme-HDR and
drop-shadow work-bound issues; historical audit claims below apply to their cited
commits and test coverage, not to cases newly identified by that review.

Corrections from that review were verified on native Linux (GCC 16.2.1, machine
with Radeon 680M): 90/90 core suites, 2/2 text suites, 4/4 UBSan suites and 5/5
installed consumers. The CPU affine/constexpr tests also pass with Clang 22.1.8.
All 125 C++ examples were compiled against the updated SDK and 111 GPU previews
were regenerated; the gallery check passes and rejects an isolated shader change.
Remaining work is listed in [TODO](TODO).

To re-audit an area, start from its audited commit:
`git diff <commit> -- <files>` shows what changed since the last check. A claim
below holds as long as its guarding tests pass.

## How areas were verified (2026-09)

Findings were accepted only with an executed reproduction program. Fixes were
independently re-verified until no correctness defect remained open. Every fix has
a regression test whose expected values are derived independently of the shader
(closed form, exact geometry, documented Photoshop/W3C behaviour, or brute force by
a different method).

## Areas

### Core and transfers
- **Operations:** deterministic GPU memory ledger (`Context::memory`,
  `set_memory_limit`, `reset_peak`), data channel for variable-length kernel data, `TransferFormat`
  (`rgba8`, `rgba16`, `rgba32_float`), image and A8 region uploads/downloads,
  `MaskTransferBufferOptions`, `Point`, `Affine`, `GradientStop`, `EdgeMode`,
  automatic command growth, atomic recording rollback, `ResourceLimits`,
  `Context::limits`, `Context::is_complete` and presentation `Submission`s,
  `Workspace`/`WorkspacePlan` with `create_workspace`, release of resources by
  their last owner (`b033b07`, `a1a7038`), and pipelines compiled on first
  submission or up front with `prepare`.
- **Verified:** staged brush data survives allocation failures and recording reuse;
  failed operations preserve prior records and release their temporary resource views;
  one-slot recorders grow for ordinary operations, filters and stroke continuation;
  every 16-bit value round-trips at five alpha levels; float transfers are
  bit exact including HDR and negatives; Affine algebra; every GPU buffer and
  texture is created in one place (`src/context.cpp`) and owned by a RAII handle
  that updates the ledger, so accounting returns to zero after destroying
  everything and budgets reject exactly the allocation that crosses them.
- **Guarding tests:** `tests/transfers.cpp`, `tests/memory.cpp`, `tests/lifetime.cpp`,
  `tests/contracts.cpp`, `tests/recording.cpp`, `tests/algorithms/workspace.cpp`,
  `tests/algorithms/affine.cpp`, `tests/algorithms/mask_controls.cpp`.
- **Open:** Emscripten build and browser execution are not validated; only the
  native Linux SDK has been run.

### Compositing
- **Operations:** 28 `BlendMode`s (W3C separable and non-separable formulas),
  `source_mask`, `preserve_alpha`, Blend If (linear or sRGB sliders), dissolve seeds,
  per-layer options in `blend_many`, positioned A8 `apply_mask`, `ColorEncoding`.
- **Audited commit:** `c768eab`.
- **Verified:** 65,536 random pixels through every mode match a double-precision
  reference (max error 3.5e-6); HDR backdrops survive neutral layers; burn never
  brightens and dodge never darkens outside [0, 1]; clipping groups built from an
  isolated base plus `preserve_alpha` match Photoshop; dissolve is stable across
  renders when seeded.
- **Guarding tests:** `tests/algorithms/compositing.cpp`,
  `tests/algorithms/compositing_contracts.cpp`, `blend_many.cpp`, `soft_light.cpp`,
  `hard_light.cpp`, `add.cpp`.
- **Known limits (header, compositing section):** `soft_light` uses the W3C formula;
  Blend If treats transparent underlying pixels as gray 0;
  the default dissolve seed is per process, so editors should store a seed per
  layer; Fill opacity and knockout are not implemented; one masked or Blend If
  layer makes `blend_many` record one pass per layer.

### Adjustments
- **Operations:** `levels`, `curves`, `lut`, `lut3d`, `color_balance`,
  `hue_saturation`, `color_matrix`, `channel_mixer`, `black_white`,
  `photo_filter`, `posterize`, `gradient_map`, shared `LevelsTransfer` and
  `ColorEncoding` controls.
- **Audited commit:** `ab21767`, no open findings.
- **Verified:** near-gray saturation stays continuous; a black-to-white gradient
  map is a gray identity; hard gradient stops; `.cube` values outside [0, 1] and
  input domains; neutral settings preserve HDR bit for bit; SDR endpoints clip like
  Photoshop while existing HDR values extend continuously.
- **Guarding tests:** `tests/algorithms/adjustments.cpp`,
  `tests/algorithms/adjustments_regressions.cpp`.
- **Known limits (header, adjustments section):** no per-range hue edits,
  `selective_color` or tetrahedral LUT interpolation; gradient maps ignore stop
  alpha differently from Photoshop (documented).

### Filters
- **Operations:** `gaussian_blur`, `box_blur`, `motion_blur`,
  `radial_blur`, `unsharp_mask`, `high_pass`, `median`, `minimum`, `maximum`,
  `pixelate`, `surface_blur`, `add_noise`.
- **Audited commit:** `43a10f7`; timings are in
  [the filter benchmarks](benchmarks/native/filters.md).
  Integration with core recording contracts is guarded by `tests/recording.cpp`.
- **Verified:** Photoshop parameter ranges accepted; heavy chains split across
  command buffers; transparent neighbours preserve edge brightness; unsharp threshold
  in sRGB levels; box sums robust with HDR; `gaussian_blur` accepts any radius with bounded cost.
  Median selects exact local float values, including flat HDR/shadow windows.
  Command capacity grows automatically, and box/round dispatches and native command
  storage are bounded. Requirements return explicit `WorkspacePlan`s, including
  rounded wavelet slots; recording uses caller-reserved buffers without a hidden
  cache. Sequential calls reuse merged plans. Workspace storage is counted once
  and retained until the last owner and submission release it.
- **Guarding tests:** `tests/algorithms/filters.cpp`,
  `tests/algorithms/filter_regressions.cpp`, `tests/algorithms/filter_large.cpp`,
  `tests/algorithms/filter_contracts.cpp`, `tests/filter_storage.cpp`, `tests/memory.cpp`.
- **Known limitation:** exact large-window medians remain slow on the integrated
  audit GPU (12 MP/r100: 25.8 s on Radeon 680M, sequential shared-GPU runs in
  the benchmarks); batching bounds individual GPU work and memory. Full-resolution
  large-window median previews are not interactive. Independent algorithm review
  and browser execution remain open.

### Geometry
- **Operations:** `transform`, `perspective`, `offset`, `ResizeFilter::area`,
  antialiased `resize` reduction, mask overloads of every geometry operation,
  `Homography`, projective `transform` overloads, `perspective_matrix`,
  `transform_bounds`, `Point` zoom centers, mask write controls and
  `resize_requirements`, `transform_requirements` and `perspective_requirements`
  for the workspace of large reductions.
- **Audited commit:** `0892a57`, no open correctness findings. Workspace
  migration `d901721`: queries and recording share one size-only plan.
- **Verified:** full-footprint reductions of any factor; `area` is exact overlap
  including rotation and shear; tiny layers keep exact coverage; far clamped
  translations pick the right edge; empty selection handles are rejected; the
  perspective horizon is exact; exactness of tiled `area` depends only on the work
  requested; affine/projective equivalence, projective bounds and fractional zoom
  centers match geometric references.
- **Guarding tests:** `tests/algorithms/transform.cpp`, `perspective.cpp`,
  `resize_reduction.cpp`, `mask_geometry.cpp`, `offset.cpp`,
  `geometry_regressions.cpp`, `geometry_workspace.cpp` (plans are exact: one
  pixel short is rejected before recording), `projective_api.cpp`,
  `mask_controls.cpp`, `bicubic.cpp`, `lanczos.cpp`; fixtures size every
  workspace through the queries.
- **Known limits (header, geometry section):** very large repeat/mirror `area`
  requests fall back to pre-averaging unless split into regions (one submission
  each); the perspective pyramid softens steep foreshortening; large reductions
  record extra commands.

### Selection
- **Operations:** `select_rectangle`, `select_ellipse`, `select_polygon`,
  `combine`, `feather`, `expand`, `contract`, `border`, `smooth`, mask
  `threshold` and `levels` with `LevelsTransfer`, `select_color_range`,
  `select_magic_wand`, `SelectionMode`, `FillRule`, `*_requirements` returning
  workspace plans (feathered marquees use `feather_requirements`). Mask edits
  accept write masks and regions; selection color distances use normalized
  controls.
- **Audited commit:** `54d353c`, no open findings. Workspace migration `aa2d2fb`.
- **Verified:** exact fill rules for overlapping, opposite, duplicated and
  off-canvas contours; expand/contract move anti-aliased edges continuously;
  magic wand matches a CPU flood fill on spirals and mazes (4- and 8-connected);
  large feathers match a direct Gaussian at canvas edges within 4/255.
- **Guarding tests:** `tests/algorithms/selection_shapes.cpp`,
  `selection_modify.cpp`, `selection_wand.cpp`, `mask_controls.cpp`.
- **Known limits (header, selection section):** feather above radius 16 runs on a
  reduced grid (about 1% error); faint feathered tails far from the 50% edge are
  not expanded; expand/contract radius up to 2000, border up to 4000.

### Painting
- **Operations:** brush engine (`Brush`, `StrokeSample`, `brush_dabs`,
  `stroke_bounds`), `brush_stroke`, `eraser_stroke`, `clone_stroke`,
  `pattern_stroke`, `dodge_burn_stroke`, `sponge_stroke`, `focus_stroke`,
  `smudge_stroke`, `gradient_fill` with replacement, `pattern_fill`,
  `paint_bucket` with `match_seed`, painting alpha lock, mask brush/eraser
  overloads, `BrushStrokeState` and `SmudgeStrokeState`.
- **Audited commits:** numerical audit `81041a1`; painting API and continuation
  `a0d092b`, with replay-growth regressions in `tests/recording.cpp`.
- **Verified:** dab spacing and pressure dynamics; opacity versus flow; thin
  elliptical tips cover their true area; sharpening keeps valid premultiplied
  output and HDR, including values near the float limit; smudge transport is exact
  across command boundaries; brushes up to 5000 px; split strokes match one call
  for image and mask destinations; failed replay growth preserves continuation state.
  Dense strokes and large smudge patches use bounded dispatches. Focus and smudge
  use explicitly reserved workspaces. Continuation snapshots are reserved through
  Context factories; reset retains their capacity without binding the next destination.
- **Guarding tests:** `tests/algorithms/brush.cpp`, `smudge.cpp`,
  `gradient_fill.cpp`, `paint_bucket.cpp`, `painting_regressions.cpp`,
  `painting.cpp`, `painting_atomic.cpp`, `stroke_state.cpp`, `tests/recording.cpp`.
- **Known limits (header, painting section):** continuation states retain a snapshot
  and replay cumulative samples, with fixed tool options and ordered submissions;
  destinations must not be edited between calls. Stateless opacity caps apply per
  call. Paint bucket is non-contiguous; use a `select_magic_wand` mask with
  `match_seed = false` for contiguous fills. Photoshop's Behind/Clear modes are absent;
  Photoshop's brush Angle field is counterclockwise, the API is clockwise.

### Analysis and viewport
- **Operations:** `histogram`, `statistics`, `sample_region`,
  `HistogramBuffer`, `StatisticsBuffer`, `ViewportOptions` for `Presenter` and
  `Display`, shared `AnalysisOptions` and `ColorEncoding`. `Display::draw`
  returns a `Submission` for waiting or completion polling.
- **Audited commit:** `15a3fbd`, no open findings.
- **Verified:** histogram counts match a CPU reference exactly; statistics stay
  finite for ±3e38; viewport minification is exact for power-of-two zooms and any
  anisotropy up to 512:1; odd sizes are unbiased; HDR averages are not clipped.
  Viewport caches are reserved explicitly and counted as presentation memory;
  draws do not grow them. Failed growth preserves prior capacity, and buffers
  from pending draws remain counted until their submission retires.
- **Guarding tests:** `tests/algorithms/histogram.cpp`, `statistics.cpp`,
  `tests/viewport.cpp`, `tests/presentation.cpp`.
- **Known limits (header, `wgpupixel_webgpu.h`):** rotated views filter the
  footprint's bounding box; the pyramid costs about a third of the image memory.

## Cross-cutting open items

- Native runtime validated only on Linux (Radeon 680M, RADV). Windows/D3D12 and
  macOS/Metal still need a full run.
- Several tests assert wall-clock bounds; on a heavily shared GPU they can fail
  and pass on rerun.

## Last full verification (2026-09-24)

At `a4645ce`, the commit that joins all areas above, a Release build passed 86/86
CTest tests and the examples check on native Linux (Radeon 680M, RADV). This is a
dated result: it does not cover later commits, other backends or performance.
