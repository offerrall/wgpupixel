# wgpupixel

The image engine under an editor: layers, selections, brushes, adjustments,
filters and transforms on the GPU with WebGPU, in linear float precision.
C++23, with native image I/O and optional typography.

Operations take an image and named options, and run when you submit them:

```cpp
auto ctx = Context::create();
Image photo = io::load(ctx, "photo.jpg");
ctx.run_and_wait([&](Commands& cmd) {
    cmd.levels(photo, {.composite = {.input_black = 0.05f, .input_white = 0.95f}});
    cmd.vignette(photo, {.radius = 0.8f, .softness = 0.4f, .color = {0, 0, 0, 1}});
});
io::save(ctx, photo, "photo-edited.png");
```

Linux is validated at runtime; Windows, macOS and the WebAssembly build compile
but are not yet validated.

The full documentation is at https://offerrall.github.io/wgpupixel/.

## Documentation

- [Overview](https://offerrall.github.io/wgpupixel/): the model, a first program and what it covers.
- [Guide](https://offerrall.github.io/wgpupixel/guide/): objects, pixels, coordinates, selections, layers, painting, memory and the viewport.
- [Operations](https://offerrall.github.io/wgpupixel/operations/): every GPU operation, with its real result.
- [Programs](https://offerrall.github.io/wgpupixel/programs/): transfers, files, memory, typography and presentation, as complete programs.
- [Build](https://offerrall.github.io/wgpupixel/build/): requirements, CMake, the browser and compatibility.

## Dependencies

- wgpu-native `==29.0.1.1` (bundled)
- ffmpeg `==9.0.1` (bundled): image codecs only
- lcms2 `>=2.16`
- zlib
- liblzma
- pangocairo `>=1.56` (optional: WGPUPIXEL_BUILD_TEXT)
- pangoft2 `>=1.56` (optional: WGPUPIXEL_BUILD_TEXT)
- cairo `>=1.18.2` (optional: WGPUPIXEL_BUILD_TEXT)
- fontconfig `>=2.15` (optional: WGPUPIXEL_BUILD_TEXT)
- freetype2 (optional: WGPUPIXEL_BUILD_TEXT)
- harfbuzz `>=2.6` (optional: WGPUPIXEL_BUILD_TEXT)
