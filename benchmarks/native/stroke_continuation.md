# Stroke continuation at 24 MP

`stroke_continuation.cpp` reproduces the editor's 200-sample diagonal stroke on a
6000×4000 image: diameter 40, two new samples per frame, opacity 0.5, final bounds
2028×1035. It includes CPU recording, submission and GPU completion. One gesture
warms the resources; three more provide 300 measured frames. No readback occurs
in the measured loop.

Build from the repository root with the native benchmark option. See
[Build](../../docs/build.md) for dependencies and offline SDK options.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DWGPUPIXEL_BUILD_BENCHMARKS=ON
cmake --build build --target wgpupixel_benchmark_stroke_continuation --parallel 8
build/benchmarks/native/wgpupixel_benchmark_stroke_continuation
build/benchmarks/native/wgpupixel_benchmark_stroke_continuation full-copy
```

To select a Vulkan driver explicitly, set `VK_DRIVER_FILES` to its ICD manifest
path in your environment before running the benchmark.

The `full-copy` control emulates the previous algorithm with public `copy` plus
one-shot `brush_stroke`: capture the entire canvas on the first frame, restore it
on subsequent frames, replay all samples. It does not run main's `continue_stroke`;
its recording overhead can differ from that implementation.
Both modes reserve one 384,000,000-byte snapshot before painting. The new algorithm
copies at most 33,583,680 logical bytes on the final frame and less on earlier
frames, versus 384,000,000 every frame in the control. GPU reads and writes each
contribute to physical memory traffic; these figures count logical copied pixels.

Measured on 2026-09-30, Release, Radeon 680M using RADV:

| Mode | Mean ms/call over 300 frames | Three gesture means, ms/call |
| --- | ---: | --- |
| Region continuation | 1.86 | 2.21, 1.58, 1.79 |
| Emulated full-copy control | 27.66 | 27.07, 27.36, 28.53 |

The GPU is shared with other work, so these timings are observations, not test
thresholds. The first gesture (including initial buffer use) is excluded in both
modes. Replay remains cumulative in stroke length; this change removes the
per-frame canvas copy, not the documented dab/data work limits.

The regression test `wgpupixel.stroke_region` uses native command interception to
count buffer-copy bytes and compute workgroups, independently of timing. Capture
and restore use at most five disjoint rectangles, totalling the accumulated bounds;
the test allows only their 8×8 workgroup padding. It also checks identical copy and
replay work for the same stroke on 512×512 and 6000×4000 canvases (1024×1024 instead
on devices whose binding limit cannot fit 24 MP). It exercises brush, eraser,
A8-mask painting and smudge. These deterministic checks run on Linux static builds;
the exact-result tests run on every build.

Each partition alternates between two different original fixtures while reusing
the snapshot reservation, with separate one-shot expectations for both fixtures.
This prevents stale snapshot contents from hiding missed capture strips. On
2026-09-30, all 11 temporary mutations were rejected by storage-byte mismatches:
dropping each of the four strips, shortening each strip by one pixel, omitting
restore, shortening restore by one pixel, and capturing after painting. All
mutations were reverted. Brush/eraser bounds are also checked for exact equality
with the accumulated union of active single-dab `stroke_bounds`, clipped before
union to exclude space between off-canvas dabs. Mask tests include widths 1–9,
all four byte alignments, narrow regions and shared row-end words.

The probe also has a `full-mask` mode:

```sh
build/benchmarks/native/wgpupixel_benchmark_stroke_continuation full-mask
```

This paints a resident 6000×4000 A8 mask with four near-central samples, diameter
8000, spacing 0, coverage 0.8 and opacity 0.5. Every call captures or restores the
entire 24 MP canvas. One four-call gesture warms the resources; ten more provide
40 measured calls, including recording, submission and completion, without readback.
The snapshot still reserves 24,000,000 bytes.

Mask snapshot copies now dispatch one lane per packed word in each rectangle row.
Interior words use one whole-word store (`atomicStore`, required by the WGSL
binding type); only partial row-end words need atomic read/modify/write to preserve
adjacent bytes. Dispatch accounting includes four bytes per mask lane and padding
for row-start alignment. The worst-case snapshot area and number of strips are unchanged.

Measured on 2026-09-30 with the same Release/RADV setup, using the same probe source
linked against the pre-change library (`dfa50ce`) and the packed-word implementation.
Three runs per version alternated before/after, after/before, before/after:

| Full-canvas A8 snapshot copy | Mean ms/call over 120 frames | Three run means, ms/call |
| --- | ---: | --- |
| Before: two atomic updates per byte | 26.87 | 27.54, 26.64, 26.44 |
| After: packed-word stores, partial-word atomic updates | 16.32 | 16.78, 15.84, 16.33 |

The complete continuation call takes about 39% less time in this workload; the
measurement includes brush replay, not just copying. Shared-GPU timings remain
observations rather than thresholds.
