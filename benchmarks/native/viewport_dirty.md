# Dirty viewport updates

`viewport_dirty.cpp` measures a 6000×4000 float32 image presented into a 1600×1000
Display at 20% zoom (1200×800 image centered with a Fit margin). Every edited frame
fills a moving 512×512 rectangle. The overlay case edits the same rectangle in an
A8 mask as well. Eight warm-up frames precede 40 measured frames per case.

Draw timing includes `Display::draw`, submission, and completion, after waiting
for the edit. The separate edit+draw timing also includes recording, submitting,
and waiting for that edit. Allocation and initial pipeline compilation are outside
the measured frames. Cases run serially on the real Radeon GPU selected with
`VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json`.

Measured on 2026-09-30, Release, GCC 16.2.1. This GPU is shared with other engineers;
these are wall-clock frame costs, not isolated GPU timestamp measurements. The
original executable was linked against unmodified main (`107b923`) before the
implementation changed, then rerun immediately before the modified executable.

| Implementation / updates | Draw min | Draw median | Draw p90 | Edit + draw median |
|---|---:|---:|---:|---:|
| Before, image | 48.437 ms | 51.934 ms | 58.797 ms | 55.590 ms |
| After, image, no dirty hint | 48.165 ms | 50.780 ms | 53.649 ms | 52.217 ms |
| After, image, dirty hint | 17.772 ms | 28.020 ms | 33.665 ms | 30.429 ms |
| Before, image + mask | 71.736 ms | 74.497 ms | 78.261 ms | 80.051 ms |
| After, image + mask, no dirty hints | 71.431 ms | 72.563 ms | 74.556 ms | 77.984 ms |
| After, image + mask, dirty hints | 28.049 ms | 30.908 ms | 35.764 ms | 39.813 ms |

Relative to the original implementation, median presentation latency improves
1.85× for image edits and 2.41× for combined image and mask edits. Full-target
rendering remains the same work; only cache reduction work is limited.

## Design and cost

Two trailing optional fields in `ViewportOptions`, `dirty_region` and
`overlay_dirty_region`, preserve existing source calls. Each describes all edits
since that presenter's/display's previous successful draw, independently for image
and mask. Omission keeps the existing revision-triggered full rebuild. Empty bounds
mean no pixels changed; dimensions must be nonnegative and bounds clip to the image.
The pure requirements query and persistent GPU cache capacities are unchanged.

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

Different resources/sizes, replaced cache buffers, failed generations, and changed
anisotropic layouts rebuild fully. A draw bypassing an outdated cache discards it:
a later draw's dirty rectangle cannot repair edits from before that intervening
draw. Invalid parameters reject before encoding or consuming dirty information.
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
API and runs the two full-rebuild cases.

## Verification

`tests/algorithms/viewport_dirty.cpp` compares complete RGBA8 target readbacks
bit for bit with separately copied sources rendered through full rebuilds. It
covers 320 deterministic random edit frames at several zooms and rotations, both
anisotropic axes, one-pixel dimensions, odd sizes, borders, empty/clipped bounds,
HDR and transparency, and independent mask edits. It also checks resource/size and
reservation changes, omitted hints, bypassed caches, queued edits, failed validation,
Display forwarding, and late failed submissions. A separate brute-force mean of
original pixels checks the 1×1 rendered image independently of the pyramid formula.
Native dispatch counting verifies that small bounds reduce work and unchanged or
empty updates dispatch no reductions.

Release configuration/build succeeded with:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DWGPUPIXEL_BUILD_TESTS=ON \
    -DWGPUPIXEL_WGPU_ROOT=/home/offerrall/photoff_ecosistema/wgpupixel/build/native/wgpu-sdk \
    -DWGPUPIXEL_FFMPEG_ROOT=/home/offerrall/photoff_ecosistema/wgpupixel/build/ffmpeg-audit/core/ffmpeg-sdk
cmake --build build -j8
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json ctest --test-dir build --output-on-failure
```

Final full suite: **91/91 passed**, 280.69 seconds. The earlier focused presentation,
viewport, and dirty-viewport run passed 3/3 in 12.20 seconds. `git diff --check` passed.

Editor follow-up: pass accumulated image and selection dirty bounds after submitting
edits; omit a bound whenever its complete extent is unknown. No additional library
work is required for this integration.
