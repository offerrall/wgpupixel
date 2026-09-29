# Filter benchmarks

Algorithms, supported ranges and documented approximations are in the filters section
of `include/wgpupixel.h`. This file records how filters were measured and the results.

## Programs

Build each program after the native library (substitute your SDK location and the
source/executable names):

```sh
c++ -std=c++23 -O2 -Iinclude benchmarks/native/filter_large.cpp \
  build/dev/libwgpupixel.a -L"$WGPUPIXEL_WGPU_ROOT/lib" -lwgpu_native \
  -Wl,-rpath,"$WGPUPIXEL_WGPU_ROOT/lib" -o build/dev/filter_large
build/dev/filter_large median 4000 3000 100
```

- `filter_large.cpp` runs one operation per process on nonconstant `noise(42)` RGB
  input: one warmup and one measured call. The timer includes recording, any
  internal allocation made while recording or submitting, submission and completion.
  Images, explicit workspace, pipeline preparation and input generation are
  created before the timer. Run heavy cases one at a time.
  The measurements below predate the explicit Workspace API: those runs reused
  internal scratch cached by the warmup. They have not been remeasured for the
  updated reservation API.
- `filters.cpp` measures Gaussian blur on 1024 × 1024 resident images seeded with
  `noise(42)`, sigma = radius / 3. Each trial records ten blurs and submits/waits
  once; two warmup trials precede seven measured trials and the result is the
  median milliseconds per blur. Transfers and pipeline preparation are excluded.
- `filter_limits.cpp` runs selected filter settings at editor-sized images of a
  given side length (1024, 3464 ≈ 12 MP, 4898 ≈ 24 MP), checks that constant images
  are preserved, prints each operation before running it, and fails if a trial
  exceeds five seconds. It is a smoke test for device resets more than a timing tool.

All timings below are end-to-end on a shared development GPU, not isolated GPU
timestamps or latency guarantees. On a shared GPU, serialize runs with
`flock ~/.cache/wgpupixel-gpu.lock`.

## Current measurements

Implementation at `43a10f7`, measured on 2026-09-24 with `filter_large.cpp`.
AMD Radeon 680M (RADV REMBRANDT), Linux, Release, GCC 16.2.1. Runs were sequential
under the GPU lock, on a machine shared with other work.

| Operation | Image | Setting | Warm call |
| --- | --- | --- | ---: |
| Median | 4000×3000 | radius 2 | 68.8 ms |
| Median | 4000×3000 | radius 100 | 25,786.5 ms |
| Median | 4000×3000 | radius 500 | 69,255.3 ms |
| Box blur | 4000×3000 | radius 1024 | 339.4 ms |
| Round maximum | 4000×3000 | radius 10 | 242.6 ms |
| Round maximum | 4000×3000 | radius 500 | 652.4 ms |

The exact median at large radii is suitable for final renders, not for interactive
dragging at full resolution; use reduced-resolution previews. Its work is split
into bounded dispatches; these runs completed without a device reset, which is not
a guarantee for every device. The large-window algorithm follows
the [2D wavelet matrix median](https://cgenglab.github.io/en/publication/sigga22_wmatrix_median/).

## Historical measurements

Measured at `7e327a2` on 2026-09-24 with `filter_large.cpp` and the method above
(one warmup, one measured call). AMD Radeon 680M (RADV REMBRANDT), Linux, Release,
GCC 16.2.1, with other test runs contending for the GPU. Filter
cascades, pyramids and dispatch bounds changed afterwards, so these numbers describe
an earlier implementation; re-measure before relying on them. Rows for median, box
and round morphology from that run are omitted because those algorithms were
replaced.

| Operation | MP | Radius / distance / amount | Measured ms |
| --- | ---: | ---: | ---: |
| gaussian | 24 | 250 | 165.07 |
| gaussian | 24 | 1000 | 103.53 |
| unsharp | 24 | 1000 | 123.27 |
| highpass | 24 | 250 | 145.82 |
| motion | 24 | 2000 | 923.04 |
| spin | 24 | 100 | 995.05 |
| zoom | 24 | 100 | 866.80 |
| surface | 24 | 100 | 1562.39 |
| square | 24 | 500 | 1243.43 |

Earlier Gaussian (`562e6ce`, `58cc6a3`) and restricted-range filter measurements are
in git history; they describe implementations that no longer exist. Versions that
submitted whole large cascades or histogram workloads together hit driver timeouts,
which is why work is now split into ordered command buffers.
