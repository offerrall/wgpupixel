# Notes for engineers and AI agents

Durable technical knowledge about wgpupixel: invariants, decisions, pitfalls and how
to verify work. Per-area verification status, known limits and open issues are in
[STATUS.md](STATUS.md). Public contracts, parameter ranges and documented
approximations live in `include/` and are the single source of truth; do not copy
them here.

## Invariants

- Images are linear, premultiplied RGBA float32 in storage buffers (16 B/pixel).
  Masks are 8-bit coverage. CPU transfers convert (`TransferFormat`: `rgba8` and
  `rgba16` are sRGB-encoded straight alpha; `rgba32_float` is the internal layout).
- HDR and negative linear values are first-class. An operation must not clip them
  unless its definition requires it, and then the header says so. Prefer continuous
  extension beyond [0, 1] (mirrored/extended sRGB, linear extrapolation) to clamps.
  Alpha and mask coverage are clamped to [0, 1].
- Coordinates: pixel (i, j) covers [i, i+1) x [j, j+1), centre (i+0.5, j+0.5).
  `Affine` maps source to destination (`x' = a x + c y + e`, `y' = b x + d y + f`),
  angles in degrees, clockwise on screen. Resamplers apply the inverse at
  destination pixel centres.
- Operations that make sense with a selection take `const Mask* mask` and
  `std::optional<Rect> region`. Validation goes through `Operation::require` with
  the public parameter name; nothing is recorded when validation fails.
- Every GPU buffer/texture uses the ledger-backed RAII owner from
  `detail::allocate_buffer`/`detail::allocate_texture`. Resources used by a submission
  stay alive and counted until it retires.
- Large GPU working storage must be reserved before recording: temporaries come
  from the caller's `Workspace`, sized by the operation's `*_requirements` query,
  which shares one size-only plan with the recording code; persistent storage
  (stroke snapshots, viewport caches) is reserved by its owner. Uniforms, staged
  kernel data, command storage and fixed binding placeholders remain internal.
  No image, temporary or cache grows behind the caller.
- Worst-case GPU cost per call must stay bounded at editor sizes (12-24 MP). A
  single long dispatch loses the device (observed on RADV); split work into bounded
  dispatches or reject truly pathological inputs with `ErrorCode::capacity`.
  Limits must sit at or above Photoshop's own ranges and be documented.

## Code layout conventions

- An operation is `src/operations/<name>.cpp` plus kernels `src/shaders/<kernel>.wgsl`
  registered in `src/kernel_list.inc`. Both directories are globbed by CMake.
- Shaders declare their build needs in header comments: `//! include <helpers>`,
  `//! coverage generated|offset|kernel|none`, `//! entry region|none`.
- Variable-length kernel data: a kernel declares
  `@group(0) @binding(7) var<storage, read> data: array<f32>;` (or `u32`/`i32`) and
  C++ stages it with `Operation::data(record, span, "parameter")` before `append`.
  See `src/utils/commands.h` and `src/shaders/parameters.wgsl`.
- `include/wgpupixel.h`, `src/kernel_list.inc` and `examples/examples.js` are split into
  per-area sections (`// ---- <area> options ----`, etc.). Add code inside the
  right section so parallel branches merge cleanly.
- Every public GPU operation needs an example in `examples/examples.js` with a rendered
  preview; `node tools/examples.mjs --check` enforces coverage and freshness.

## Verifying work

```sh
cmake -S . -B build/dev -G Ninja -DCMAKE_BUILD_TYPE=Release -DWGPUPIXEL_BUILD_TESTS=ON
cmake --build build/dev --parallel 4 && (cd build/dev && ctest --output-on-failure -j2)
node tools/examples.mjs --check
```

Rendering previews and regenerating docs/operations.md and docs/programs.md follows the
"Examples" notes at the end of docs/build.md. Accepted evidence:

- A CPU reference is only evidence when it is derived independently of the shader:
  closed-form values, geometric truth (exact areas), documented Photoshop/W3C
  behaviour, or brute force by a different method. References that restate the
  shader formula let real bugs pass; every review round in 2026-09 found this.
- Test at least one size larger than a workgroup, plus mask, region, alpha 0,
  tiny alpha and HDR/negative inputs.
- Performance claims name the image size, the GPU and whether runs were isolated.

## Decisions

- Storage buffers of float32 rather than textures: exact read-write on every
  backend including the browser, full precision and HDR, simpler kernels. The cost
  is memory (about 384 MB per 24 MP image). Consumers handle memory through caching
  (flattened stacks, offloading layers in `rgba16`, `revision()` as cache key).
  Revisit only with measurements; an `rgba16float` storage mode is the likely next
  step and would not change the API.
- Document models, layer stacks, history and UI belong to consumers. The library
  provides primitives (blend with source masks, alpha lock, Blend If, geometry for
  masks, viewport presentation) so a layer stack is straightforward to build.

## Pitfalls already found

- naga miscompiles pointers to array elements passed into functions
  (`src/shaders/statistics.wgsl`); return values instead.
- On RADV (Radeon 680M) plain storage writes by one invocation were not reliably
  visible to others in the same workgroup after `storageBarrier()` alone. The
  sequential smudge kernel uses atomics (`src/shaders/smudge_atomic.wgsl`).
- The GPU compiler may reorder float arithmetic: "scale then subtract" still
  overflowed near 3e38. Normalise weights before accumulating.
- Very small reciprocals flush to zero on the GPU; keep scale factors within
  2^126.
- Kernels sharing helpers across areas must include all of them
  (`//! include blend_modes blend_pixel ...`); a missing helper only shows up as a
  pipeline creation failure that makes every test fail.
- Adding an overload to an existing operation name requires the examples' `covers`
  to name the overload (`'threshold:Image'`).
