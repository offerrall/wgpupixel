# Guide

Eight short chapters, one mechanism each. Read them in order the first time;
afterwards each stands on its own.

## Objects and lifetime

A `Context` owns the GPU device and creates the resources: an `Image` holds RGBA
pixels, a `Mask` holds one coverage value per pixel, and upload and readback
buffers carry bytes between your memory and the GPU.

You never call the GPU directly. You record operations into `Commands`, submit
them, and wait when you need the result. Recording checks arguments at once, and
if an operation throws, the work recorded before it stays intact.

```cpp
auto ctx = Context::create();
Image canvas = ctx.create_image({.width = 1920, .height = 1080});
UploadBuffer upload = ctx.create_upload_buffer(canvas);
ctx.write(upload, pixels);                  // RGBA8 bytes from your file or UI

Commands cmd = ctx.create_commands();
cmd.upload(upload, canvas);
cmd.exposure(canvas, {.stops = 0.5f});
Submission done = ctx.submit(cmd);          // the GPU starts now
// ... record the next batch in another Commands here ...
ctx.wait(done);                             // or poll ctx.is_complete(done)
```

For one-off edits, `ctx.run_and_wait([&](Commands& cmd) { ... })` records,
submits and waits in one call; if recording throws, nothing is submitted.

A submission keeps every resource it touches alive until the GPU is done, so
dropping a handle is always safe: storage is released when its last owner lets
go. An explicit `ctx.destroy` refuses a resource that recorded or pending work
still uses.

## Pixels: linear, premultiplied, float

Inside the GPU every pixel is four 32-bit floats: red, green and blue in
**linear light**, already **multiplied by alpha**. Files and screens use sRGB
values with separate alpha, so transfers convert on the way in and out:

```text
in your file     sRGB, separate alpha     128  64   32  · 128
linear light     physical intensity       .216 .051 .014 · .502
on the GPU       linear × alpha, float    .108 .026 .007 · .502
```

Each transfer buffer chooses its format: `rgba8` and `rgba16` carry sRGB values
with separate alpha, like 8- and 16-bit files; `rgba32_float` is the internal
layout, so it round-trips exactly, including values above 1 and below 0.

```cpp
// Read a finished image back as 16-bit, like a 16-bit file.
auto readback = ctx.create_readback_buffer(canvas, {.format = TransferFormat::rgba16});
ctx.run_and_wait([&](Commands& cmd) { cmd.download(canvas, readback); });
std::vector<std::uint8_t> bytes(1920 * 1080 * readback.bytes_per_pixel());
ctx.read(readback, bytes);
```

Premultiplied storage is what keeps edges right: every filter, resize and blend
averages neighbouring pixels, and with separate alpha the invisible colour of a
transparent pixel would leak into its neighbours as a fringe. A colour you pass
in options is stored the same way: 50% opaque red is `{0.5f, 0, 0, 0.5f}`.
A colour picker gives sRGB with separate alpha; `from_srgb` converts it with the
same curve as the `rgba8`/`rgba16` transfers, and `to_srgb` goes back:

```cpp
Color red_half = from_srgb(1, 0, 0, 0.5f);            // {0.5f, 0, 0, 0.5f}
Color gray = from_srgb(0.5f, 0.5f, 0.5f);             // {0.214f, 0.214f, 0.214f, 1}
std::array<float, 4> picked = to_srgb(gray);          // {0.5f, 0.5f, 0.5f, 1}
```

## Coordinates and resampling

Pixel `(i, j)` covers the square from `(i, j)` to `(i+1, j+1)`, with its centre
at `(i+0.5, j+0.5)`. The y axis points down and angles turn clockwise on screen.

An `Affine` says where source pixels land in the destination. To produce each
output pixel, the GPU maps the destination pixel's centre back into the source
with the inverse, and filters what it finds there. When a transform shrinks the
image, the filter widens to the whole footprint, and `ResizeFilter::area`
weights each source pixel by its exact overlap, so fine detail averages instead
of aliasing into moiré.

```cpp
Affine placement = Affine::translate(400, 300) * Affine::rotate(15) * Affine::scale(0.5f);
Rect bounds = transform_bounds({0, 0, 1000, 800}, placement);   // where the layer lands
cmd.transform(layer, placed, {.matrix = placement, .filter = ResizeFilter::bicubic});
```

`a * b` applies `b` first, and `inverse(matrix)` returns an empty optional for a
singular matrix.

## Selections: masks and regions

A selection is a `Mask`: one coverage value per pixel, from 0 (untouched) to 255
(fully affected); soft edges are the values in between. Almost every image
operation accepts two optional limits:

```text
written = original + (result − original) × mask coverage,   only inside region
```

`region` is a rectangle outside of which nothing is written, and it also saves
the work there: pass the selection's bounding box and the GPU skips the rest.

Selection operations write into a mask with a `SelectionMode`: `replace`, `add`,
`subtract`, `intersect` or `difference`, like holding Shift or Alt with a
marquee tool. Modes combine coverage exactly, so soft edges merge without halos.

```cpp
Mask selection = ctx.create_mask(canvas.size());
Workspace workspace = ctx.create_workspace(feather_requirements(canvas.size(), {.radius = 12}).workspace);
ctx.run_and_wait([&](Commands& cmd) {
    cmd.select_ellipse(selection, {.origin = {200, 120}, .width = 480, .height = 320});
    cmd.select_rectangle(selection, {.origin = {150, 380}, .width = 600, .height = 120,
                                     .mode = SelectionMode::add});
    cmd.feather(selection, {.radius = 12, .workspace = workspace});
    cmd.hue_saturation(canvas, {.saturation = 0.4f, .mask = &selection,
                                .region = Rect{110, 80, 680, 460}});
});
```

Operations that need temporary GPU memory, like `feather` above, take a
`Workspace`. Their `*_requirements` query returns its plan; merge the plans of
operations you run in sequence, create one workspace and reuse it. The library
never allocates it for you.

To find a selection's exact bounds after a wand or other edit, create a
`MaskBoundsBuffer` with `ctx.create_mask_bounds_buffer()` and record
`cmd.mask_bounds(selection, result)` after the edit. Submit, then wait or poll
`ctx.is_complete(done)` before calling `ctx.read(result)`. The returned
`std::optional<Rect>` is empty when nothing is selected; otherwise it holds a
half-open rectangle in mask coordinates, ready for cropping or an operation's
`region`. The default threshold is `1.0f / 255.0f`, the smallest nonzero coverage.
`threshold` is an inclusive minimum in [0, 1], converted to an A8 byte with
`ceil(threshold * 255)`: any value in (0, 1/255] measures coverage > 0, while 0
includes zero coverage. `region` restricts the query. Only 16 bytes are read
back, and no workspace is needed.

## A layer stack

wgpupixel has no document object: your application owns the layers. Rendering
the document is a sequence of blends from the bottom layer to the top, each with
that layer's options:

```cpp
cmd.fill(canvas, {.color = {1, 1, 1, 1}});                         // background
cmd.blend(photo, canvas, {.position = {120, 80}, .opacity = 0.9f,  // a layer with a mask
                          .mode = BlendMode::multiply, .source_mask = &photo_mask});
cmd.copy(shape, group);                                            // a clipping group:
cmd.blend(texture, group, {.preserve_alpha = true});               // texture only inside shape
cmd.blend(group, canvas, {});
```

A layer mask travels with its layer as `source_mask`; `preserve_alpha` is Lock
Transparent Pixels; a clipping group is a small stack rendered into a copy of
its base. Cache the layers you are not editing and re-blend only the ones that
changed. All 28 blend modes use the W3C compositing formulas on linear values,
extended past 1 for HDR, and Blend If can read linear or sRGB grey.

## Painting

Your application collects stroke samples from the mouse or pen: a position and a
pressure. The brush engine places a dab every `spacing × diameter` pixels along
the path, interpolating the pressure between samples. `flow` is how much each
dab adds, and `opacity` caps what the whole stroke can reach.

```cpp
std::vector<StrokeSample> samples = {{{100, 400}, 0.2f}, {{300, 250}, 0.7f}, {{520, 300}, 1.0f}};
auto stroke = ctx.create_brush_stroke_state(canvas);   // reserve before the gesture
cmd.brush_stroke(canvas, stroke, {.samples = samples,
                          .brush = {.diameter = 40, .hardness = 0.6f, .spacing = 0.1f,
                                    .minimum_size = 0.2f},
                          .color = {0.8f, 0.2f, 0.05f, 1}, .opacity = 0.7f, .flow = 0.3f});
```

A `BrushStrokeState` (or `SmudgeStrokeState`) keeps the stroke across calls: pass
only the new samples each time, with the same state and options, and keep the
destination, tip and selection unchanged. `reset()` starts the next gesture and
keeps the reserved capacity. The eraser, clone and pattern stamp, dodge and burn,
sponge, blur and sharpen, and smudge all take the same `Brush` and samples, and
brush and eraser also paint into a `Mask`.

## Memory

An image costs 16 bytes per pixel: a 24-megapixel layer takes 384 MB of GPU
memory. Every GPU allocation goes through one ledger. `ctx.memory()` reports what
is in use by category (images, masks, transfer buffers, workspaces, presentation,
internal) and the peak, and `set_memory_limit` sets a budget that rejects an
allocation before it goes over.

```cpp
ctx.set_memory_limit(std::uint64_t{3} << 30);        // 3 GiB for this document
MemoryUsage used = ctx.memory();
if (used.total > (std::uint64_t{5} << 29)) {
    // Evict an inactive layer: read it back as rgba16, then ctx.destroy it.
}
```

The library accounts; your application decides. `revision()` on an image or mask
changes whenever its pixels change, which makes it a cache key. `Context::limits()`
reports the per-resource limits, including `max_texture_dimension_2d`: the device
opens with the adapter's own 2D texture limit (commonly 16384), so `Display` and
`Presenter` targets may be wider or taller than WebGPU's default 8192, for example a
window spanning several monitors or a large offscreen render. Heavy operations split
their work into bounded dispatches and submissions, and a size or work budget they
cannot meet is an `ErrorCode::capacity`.

## The viewport

A `Display`, or a `Presenter` drawing into your own texture, shows an image with a
view transform: pan, zoom and rotation, with the transparency checkerboard behind
it and the pasteboard colour around it.

```cpp
auto display = webgpu::Display::create(ctx, 1600, 1000);
webgpu::ViewportOptions view{.view = Affine::translate(pan_x, pan_y) * Affine::scale(zoom),
                            .overlay = &selection, .pixel_grid = true};
display.reserve(canvas.size(), view);
Submission shown = display.draw(canvas, view);
// Register display.view() with your UI toolkit as the canvas texture.
```

At 100% and above, image pixels are drawn crisp, with an optional pixel grid above
`pixel_grid_zoom` (500% by default). Below
100%, each screen pixel averages its footprint from the closest level of a
pyramid built once per image revision, so detail never shimmers while zooming
out. A selection passed as `overlay` is tinted like Quick Mask.

`webgpu::viewport_requirements` reports the cache bytes a view needs, and
`reserve` grows the caches only when a view needs more; a draw without enough
reserved capacity is an `ErrorCode::capacity`, it never grows the cache itself.

<details>
<summary>How it works inside</summary>

**From call to dispatch.** A public operation does no GPU work. It validates its
arguments, turns them into one or more records (a kernel, its resources, 144
bytes of parameters and an optional slice of data) and appends them to the
recording. If any check fails, nothing it staged remains. Submitting uploads
each record's parameters once, encodes the dispatches (masked or plain, clipped
to the region) into bounded command-buffer batches, and queues them. Each batch
holds references to its resources until the GPU reports completion; then it
retires and the memory ledger is updated. GPU buffers are created in one place,
`src/context.cpp`, and owned by RAII handles, so the ledger cannot drift.

**How shaders are built.** Kernels are WGSL files in `src/shaders/`, listed in
`src/kernel_list.inc`. Comment directives at the top declare their helpers
(`//! include ...`), their coverage mode and their entry mode.
`cmake/EmbedShaders.cmake` resolves them and embeds the sources. By default the
build adds shared entry points around the kernel's `apply_operation`: a plain one
that writes the result, and a masked one that mixes result and original by
coverage (`coverage offset` also accounts for the source placement,
`coverage kernel` applies coverage itself, `coverage none` has no masked
pipeline). `//! entry none` kernels provide their own entry points. Variable-length
data arrives at `@binding(7)` (`src/shaders/parameters.wgsl`). A pipeline is
created on first use, or all of them by `Context::prepare()`.

**Adding an operation.**

1. Declare the options struct and the `Commands` method in the right area section
   of `include/wgpupixel.h`, with ranges and units next to each field.
2. Write `src/operations/<name>.cpp`: validate every argument with
   `Operation::require` using the public parameter name, stage variable-length
   data with `Operation::data`, then append records.
3. Write the kernel in `src/shaders/`, list it in `src/kernel_list.inc`, and keep
   the worst-case cost of one dispatch bounded.
4. Add tests in `tests/algorithms/` whose expected values come from an
   independent derivation, including masks, regions, transparency and HDR input.
5. Add it to [Operations](operations.md): a short snippet and the image it produces.

**How work is verified.** Every area was built by one engineer and reviewed by
another who did not write it. A finding counts only with a program that
reproduces it, and a fix only with a test whose expectations were derived
independently. In every review round, the bugs that slipped through had tests
whose reference copied the shader's formula; closed forms, exact geometry and
brute force by a different method caught them.

</details>
