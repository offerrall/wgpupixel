# Overview

wgpupixel is the image engine under an editor: layers, selections, brushes,
adjustments, filters and transforms on the GPU, in linear float precision. Your
application owns the document, the UI and the history; wgpupixel does the pixels.

![Perspective distortion](images/examples/perspective.png)

## The model

Your program records operations on the CPU, and the GPU owns the pixels:

```text
your program (CPU)                         the GPU
  pixels in  ── ctx.write + upload ──►  Image · Mask   linear, premultiplied float32 RGBA
  Commands: levels, gaussian_blur, ...  every operation reads and writes here
  pixels out ◄── download + ctx.read ──  Display       on screen
```

Nothing runs while you record. `Commands` collects operations, `submit` sends
them to the GPU in order, and `wait` returns when they are done. Uploads and
downloads are operations too, so a whole edit is one submission.

Every operation takes its image and a struct of named options, such as
`cmd.brightness(image, {.amount = 0.2f})`, and most accept a selection `mask` and
a `region`. A wrong argument throws at once, naming the parameter.

## A first program

```cpp
#include <wgpupixel.h>
#include <wgpupixel_io.h>
using namespace wgpupixel;

int main() {
    auto ctx = Context::create();
    Image photo = io::load(ctx, "photo.jpg");
    ctx.run_and_wait([&](Commands& cmd) {
        cmd.levels(photo, {.composite = {.input_black = 0.05f, .input_white = 0.95f}});
        cmd.vignette(photo, {.radius = 0.8f, .softness = 0.4f, .color = {0, 0, 0, 1}});
    });
    io::save(ctx, photo, "photo-edited.png");
    ctx.destroy(photo);
}
```

How to build and link it is in [Build](build.md).

## What it covers

| Area | What it has |
| --- | --- |
| Compositing | 28 blend modes, layer masks, alpha lock, Blend If, clipping groups |
| Selections | marquees, lasso, color range, exact magic wand, feather, expand, contract |
| Painting | pressure brushes, eraser, clone, dodge and burn, smudge, gradients, bucket |
| Adjustments | levels, curves, 1D and 3D LUTs, hue/saturation, color balance, gradient map |
| Filters | Gaussian at any radius, unsharp mask, median, motion and radial blur, surface blur |
| Geometry | free transform, perspective, offset and area-exact reduction, for images and masks |
| Analysis | histograms and statistics with exact counts, eyedropper sampling |
| Viewport | pan, zoom and rotate on screen, with a pyramid, checkerboard and quick-mask overlay |

Every operation, with its real result, is in [Operations](operations.md);
transfers, files, memory, typography and presentation are complete programs in
[Programs](programs.md).

## Guarantees

- **Precision first.** Linear, premultiplied float32 everywhere. HDR and
  negative values pass through operations that do not define a clip.
- **Bounded cost.** No accepted parameter can stall the GPU at editor sizes;
  heavy work is split into bounded submissions.
- **Deterministic memory.** Every GPU byte is accounted. Set a budget and
  allocations fail at the same point on every machine.

## Status

Linux is validated at runtime. Windows and macOS build with CMake but are not
yet validated, and the WebAssembly build compiles but has not been run in a
browser.
