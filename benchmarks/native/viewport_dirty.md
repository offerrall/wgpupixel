# Dirty viewport updates

`viewport_dirty.cpp` measures a 6000×4000 float32 image presented into a 1600×1000
Display at 20% zoom (1200×800 image centered with a Fit margin). Every edited frame
fills a moving 512×512 rectangle. The overlay case edits the same rectangle in an
A8 mask as well. Each case runs five times, with eight warm-up frames followed by
40 measured frames per run; case order rotates between runs. The no-edit cases
reuse the same image/mask revisions and dispatch no reductions.

Draw timing includes `Display::draw`, submission, and completion, after waiting
for the edit. The separate edit+draw timing also includes recording, submitting,
and waiting for that edit. Allocation and initial pipeline compilation are outside
the measured frames. Cases run serially on the real Radeon GPU selected with
`VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json`.

Measured on 2026-09-30, Release, GCC 16.2.1. This GPU is shared with other engineers;
these are wall-clock frame costs, not isolated GPU timestamps. The earlier report's
51.9/74.5 ms baseline and 28.0/30.9 ms dirty timings were inflated by other GPU work
and are superseded here. The controls below use the current implementation with
hints omitted; they are not historical before/after binaries.

The reported batch started after ten consecutive one-second 0% GPU-busy samples,
after the competing test runs and bulk C++ compilation ended. Process monitoring
observed no other test/benchmark jobs during the batch, and a spot check found no
compiler processes. The GPU clock was observed at 1471 MHz in automatic mode;
no GPU settings or other processes were changed. Desktop rendering remained active.
An exploratory batch with heavy CPU compilation had a 16.429 ms image no-edit
median and an observed 400 MHz GPU clock; it is excluded from the quiet table.

Each reported median is the median of five 40-frame run medians.
The range shows the smallest/largest run median, not individual frame extremes.

| Current implementation / updates | Draw median | Run-median range | Edit + draw median |
|---|---:|---:|---:|
| Image, full rebuild | 25.148 ms | 25.038–25.452 ms | 25.798 ms |
| Image, dirty hint | 5.062 ms | 4.898–5.093 ms | 5.538 ms |
| Image, no edit | 4.647 ms | 4.357–4.818 ms | 4.647 ms |
| Image + mask, full rebuild | 30.755 ms | 30.634–31.084 ms | 33.175 ms |
| Image + mask, dirty hints | 7.165 ms | 6.897–7.437 ms | 9.377 ms |
| Image + mask, no edit | 6.567 ms | 6.215–7.115 ms | 6.567 ms |

For audit, the five draw medians in run order were:

| Case | Run 1 | Run 2 | Run 3 | Run 4 | Run 5 |
|---|---:|---:|---:|---:|---:|
| Image, full | 25.246 | 25.148 | 25.452 | 25.038 | 25.122 |
| Image, dirty | 4.898 | 5.063 | 4.913 | 5.093 | 5.062 |
| Image, no edit | 4.524 | 4.818 | 4.816 | 4.357 | 4.647 |
| Image + mask, full | 30.755 | 31.084 | 30.668 | 30.634 | 31.030 |
| Image + mask, dirty | 7.275 | 6.968 | 6.897 | 7.165 | 7.437 |
| Image + mask, no edit | 6.578 | 7.115 | 6.329 | 6.215 | 6.567 |

All entries are milliseconds. Relative to the current full-rebuild control, dirty
updates reduce median draw latency 4.97× for the image and 4.29× for image + mask.
The measured no-edit floor is 4.65/6.57 ms here; the reviewer's separate ~8 ms floor
and the CPU-busy batch show why it should not be treated as a fixed machine constant.

The no-edit floor isolates the final full-screen render and submission/completion
cost from cache generation. At 20% zoom it samples level 1 (2×2 source pixels per
texel): `image_texel` reads four scalar storage-buffer floats, and image/overlay
sampling separately evaluate the footprint and weights. Region hints reduce cache
work, while this final rendering work remains unchanged.

## Design and cost

Two optional `DirtyHint` draw arguments carry independent image and overlay bounds
plus the resource revision captured before those edits. `ViewportOptions` is reusable
configuration for `reserve`, `viewport_requirements` and draws. Partial work requires
the cached revision to equal the hint's `since_revision`; a mismatch rebuilds fully.
Omission keeps the existing revision-triggered full rebuild. Empty bounds assert no
pixels changed; dimensions must be nonnegative and bounds clip to the image. The
caller must cover every edit since the specified revision: missing edits cannot be
detected. The pure requirements query and persistent GPU cache capacities are unchanged.

A changed rectangle maps to `[floor(start/2), ceil(end/2))` on each axis at every
pyramid level. Dispatches cover only that rectangle; the shader retains the same
source coordinates, weights, and floating-point arithmetic order. The anisotropic
cache maps the rectangle through its source level and then its reduction factor.
All pyramid levels stay updated, including levels not sampled in the current view.

At 24 MP there are 13 levels and 8,000,189 cached texels. A representative 512×512
edit at (2500,1700) recomputes 87,648 texels in 1,406 workgroups, versus 125,192
workgroups for the full pyramid. Image cache storage remains 128,003,024 bytes;
the optional float mask pyramid adds 32,000,756 bytes. Each level still has one
bounded reduction dispatch, with at most four source reads per isotropic output.
Dirty bounds add 16 uniform bytes per dispatch (the native aligned uniform stride
stays unchanged). Small CPU dependency lists retain pending generations until
success is known, so late failures invalidate all dependent partial updates.

Different resources, any intervening size changes (including A→B→A), mismatched
revisions, replaced cache buffers, failed generations, and changed anisotropic
layouts rebuild fully. A private 64-bit size counter adds eight CPU bytes per
resource and per cache; it does not allocate GPU storage. A draw bypassing an outdated
cache discards it: a later draw rebuilds it conservatively. Revision checks independently
protect views that skip frames and draws rejected before submission. Invalid parameters
reject before encoding or consuming dirty information.
There is no new workspace and no new kernel; existing reduction entry points gain
a dispatch origin and extent.

## Reproduce

After the Release build described in the task rules, from the worktree root:

```sh
viewport_sdk=/home/offerrall/photoff_ecosistema/wgpupixel/build/native/wgpu-sdk
g++ -O3 -std=c++23 -DWGPUPIXEL_VIEWPORT_DIRTY \
    benchmarks/native/viewport_dirty.cpp -Iinclude -I"$viewport_sdk/include" \
    build/libwgpupixel.a -L"$viewport_sdk/lib" -lwgpu_native \
    -Wl,-rpath,"$viewport_sdk/lib" -o build/viewport_dirty_after
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json build/viewport_dirty_after
```

To measure an original checkout, compile this same source against that checkout's
headers/library without `-DWGPUPIXEL_VIEWPORT_DIRTY`. It then uses only the original
API and runs the full-rebuild and no-edit cases.

## Verification

`tests/algorithms/viewport_dirty.cpp` compares complete RGBA8 target readbacks
bit for bit with separately copied sources rendered through full rebuilds. On native
Linux static builds, a linker wrapper adds test-only CopySrc usage to cache allocations;
readbacks compare every float32 bit in all required image, mask and anisotropic cache
buffers, including levels not sampled by that view. No public API or production
buffer usage was added for this inspection.

Coverage includes 320 deterministic random edit frames, rotation, both anisotropic
axes, one-pixel dimensions, odd sizes, borders, empty/clipped bounds, HDR,
transparency, independent image/mask edits and every pyramid level, including 1×1
(zoom down to `1 / 2^(count+1)`). A brute-force original-pixel mean independently
checks the top three levels of a 65×65 source. Regressions cover per-frame hints
shared by presenters drawing at different cadences (P2), a capacity-rejected draw
followed by a new per-frame hint (P3), and A→B→A between draws (P4), for isotropic
and anisotropic caches, including source-level axis reductions. Existing identity,
reservation, omitted-hint, bypass, queue-order and late-failure cases remain.
Native dispatch counting verifies small bounds reduce work and unchanged/empty
updates dispatch no reductions.

Release configuration/build succeeded with:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DWGPUPIXEL_BUILD_TESTS=ON \
    -DWGPUPIXEL_WGPU_ROOT=/home/offerrall/photoff_ecosistema/wgpupixel/build/native/wgpu-sdk \
    -DWGPUPIXEL_FFMPEG_ROOT=/home/offerrall/photoff_ecosistema/wgpupixel/build/ffmpeg-audit/core/ffmpeg-sdk
cmake --build build -j8
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json ctest --test-dir build --output-on-failure
```

Four temporary mutations were rejected on the real GPU, then reverted and rebuilt:

| Mutation | Regression that failed |
|---|---|
| Remove the `since_revision` check | P2 and P3, float cache mismatch |
| Remove the size-history check | P4, float cache mismatch |
| Skip the last level on all rebuilds | Independent original-pixel mean |
| Skip the last level only on partial rebuilds | Float cache comparisons, every-level views and independent mean |

Final full real-GPU suite: **91/91 passed**, 260.15 seconds, including the expanded
dirty-viewport suite (16.72 seconds). `git diff --check` passed.


Follow-ups (not implemented): redraw only the projected dirty rectangle with
`LoadOp_Load` plus scissor; vec4/texture texel reads; share image/overlay footprint
math; coarser level bias. The previously observed ~26 ms level-0 sampling cost is
also pre-existing and remains outside this change.
