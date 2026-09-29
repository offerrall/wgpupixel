// The only source of examples. Add an entry; tools/examples.mjs renders it into the docs and checks coverage.
const blue = '{0.025f, 0.16f, 0.42f, 1}';
const orange = '{0.95f, 0.24f, 0.055f, 1}';
const paper = '{0.88f, 0.85f, 0.77f, 1}';
const example = (id, group, title, description, body, options = {}) =>
  ({id, group, title, description, body, covers: [id], ...options});
export const examples = [
  example('fill', 'Create', 'Fill a color', 'Fill the entire image with a linear, premultiplied RGBA color. For 50% opaque red use {0.5f, 0, 0, 0.5f}.', `cmd.fill(image, {.color = ${orange}});`, {seed: false, covers: ['fill:Image']}),
  example('checkerboard', 'Create', 'Checkerboard', 'Cell size and offset use pixels. Generate this in a separate image when it is only a transparency preview.', `cmd.checkerboard(image, {.size = 24, .first = ${blue}, .second = ${paper}, .offset = {12, 0}});`, {seed: false}),
  example('grid', 'Create', 'Grid', 'Spacing and line width use pixels. The background is written as well as the grid lines.', `cmd.grid(image, {.spacing = 32, .line_width = 2, .color = ${blue}, .background = ${paper}});`, {seed: false}),
  example('stripes', 'Create', 'Stripes', 'Angle is in degrees; spacing, width and offset are in pixels.', `cmd.stripes(image, {.angle = 35, .spacing = 40, .width = 16, .first = ${blue}, .second = ${paper}});`, {seed: false}),
  example('dots', 'Create', 'Dots', 'Spacing, radius and edge softness use pixels. Offset shifts the repeating pattern.', `cmd.dots(image, {.spacing = 40, .radius = 10, .color = ${orange}, .background = ${paper}, .offset = {0, 0}, .softness = 2});`, {seed: false}),
  example('circle', 'Create', 'Circle', 'Fits a circle inside the image. Foreground, background and edge softness are explicit; a transparent background makes a reusable stamp.', `cmd.circle(image, {.color = ${orange}, .background = {0, 0, 0, 0}, .softness = 2});`, {seed: false}),
  example('polygon', 'Create', 'Polygon', 'Choose the side count, rotation in degrees and edge softness in pixels.', `cmd.polygon(image, {.sides = 6, .color = ${blue}, .background = ${paper}, .rotation = 30, .softness = 2});`, {seed: false}),
  example('noise', 'Create', 'Noise', 'A fixed seed produces reproducible noise. Set monochrome to false for independent color channels.', 'cmd.noise(image, {.seed = 42, .monochrome = true});', {seed: false}),
  example('perlin', 'Create', 'Perlin noise', 'Scale controls feature size; octaves add detail. Persistence controls amplitude and lacunarity controls frequency between octaves.', `cmd.perlin(image, {.scale = 64, .seed = 42, .octaves = 4, .persistence = 0.5f, .lacunarity = 2, .first = ${blue}, .second = ${paper}});`, {seed: false}),
  example('copy', 'Transform', 'Copy pixels', 'Copy between equally sized images. An optional coverage mask or destination region limits what is overwritten.', 'cmd.copy(image, result);', {buffers: {result: [320, 200]}, output: 'result', covers: ['copy:Image']}),
  example('resize', 'Transform', 'Resize', 'Destination dimensions set the output size. Filters: nearest, bilinear, bicubic, lanczos and area; reductions average instead of aliasing. Source and destination must be distinct. Very large reductions run one pass per axis through a workspace sized by resize_requirements.', 'cmd.resize(image, result, {.filter = ResizeFilter::bilinear});', {buffers: {result: [160, 100]}, output: 'result'}),
  example('crop', 'Transform', 'Crop', 'The origin is in source pixels. Destination dimensions define the crop size; no resampling is performed.', 'cmd.crop(image, result, {.origin = {80, 40}});', {buffers: {result: [160, 120]}, output: 'result', covers: ['crop:Image']}),
  example('flip', 'Transform', 'Flip', 'Choose horizontal, vertical or both. Read from one image and write to another.', 'cmd.flip(image, result, {.direction = FlipDirection::horizontal});', {buffers: {result: [320, 200]}, output: 'result', covers: ['flip:Image']}),
  example('rotate', 'Transform', 'Rotate', 'Angle is in degrees. Rotation uses the image centers; uncovered pixels are transparent. Choose the destination size explicitly.', 'cmd.rotate(image, result, {.degrees = 25, .filter = ResizeFilter::bicubic});', {buffers: {result: [320, 200]}, output: 'result', covers: ['rotate:Image']}),
  example( 'zoom', 'Transform', 'Zoom', 'Scale around a center expressed in source pixels. Unlike resize, the destination can keep the original dimensions.', 'cmd.zoom(image, result, {.factor = 1.6f, .center = {160, 100}, .filter = ResizeFilter::lanczos});', {buffers : {result : [ 320, 200 ]}, output : 'result', covers : [ 'zoom:Image' ]}),
  example( 'pixelate_resize_recipe', 'Transform', 'Pixelate', 'A recipe made from two resizes: reduce detail, then enlarge with nearest-neighbor sampling. Use pixelate for exact cell averages.', `cmd.resize(image, small, {.filter = ResizeFilter::bilinear});
cmd.resize(small, image, {.filter = ResizeFilter::nearest});`, {buffers : {small : [ 20, 12 ]}, covers : [ 'resize' ]}),
  example('grayscale', 'Color', 'Grayscale', 'Convert color to grayscale in place. Alpha is preserved; the library works in linear light.', 'cmd.grayscale(image);'),
  example('brightness', 'Color', 'Brightness', 'Add an amount to straight linear RGB while preserving alpha. Negative values darken; use exposure for photographic stops.', 'cmd.brightness(image, {.amount = 0.12f});'),
  example('exposure', 'Color', 'Exposure', 'Multiply linear RGB by 2 raised to the number of stops. +1 doubles the light; −1 halves it.', 'cmd.exposure(image, {.stops = 0.75f});'),
  example('contrast', 'Color', 'Contrast', 'Change contrast around the linear RGB pivot. A factor of 1 leaves the image unchanged; 0 collapses contrast.', 'cmd.contrast(image, {.factor = 1.5f});'),
  example('saturation', 'Color', 'Saturation', 'A factor of 0 removes saturation, 1 preserves it, and values above 1 increase it.', 'cmd.saturation(image, {.factor = 0.35f});'),
  example('gamma', 'Color', 'Gamma', 'Apply the library’s gamma adjustment to straight RGB. Gamma must be positive; alpha is unchanged.', 'cmd.gamma(image, {.value = 1.8f});'),
  example('opacity', 'Color', 'Opacity', 'Scale premultiplied RGB and alpha together so translucent colors remain valid.', 'cmd.opacity(image, {.factor = 0.5f});'),
  example('hue', 'Color', 'Hue', 'Rotate hue by an angle in degrees while preserving alpha.', 'cmd.hue(image, {.degrees = 100});'),
  example('vibrance', 'Color', 'Vibrance', 'Adjust color intensity with saturation-dependent weighting. Zero leaves the image unchanged.', 'cmd.vibrance(image, {.amount = 0.65f});'),
  example('sepia', 'Color', 'Sepia', 'Blend the original colors with a sepia treatment. Intensity controls the effect.', 'cmd.sepia(image, {.intensity = 0.85f});'),
  example('invert', 'Color', 'Invert color', 'Invert straight RGB and preserve alpha. Mask inversion is shown separately.', 'cmd.invert(image);', {covers: ['invert:Image']}),
  example('threshold', 'Color', 'Threshold', 'Convert luminance into a two-tone image using a linear threshold; keep alpha.', 'cmd.threshold(image, {.value = 0.25f});', {covers: ['threshold:Image']}),
  example('solarize', 'Color', 'Solarize', 'Invert channels above the selected threshold, keeping the remaining values and alpha.', 'cmd.solarize(image, {.value = 0.4f});'),
  example('sobel', 'Effects', 'Sobel edges', 'Detect local edges in a separate destination image.', 'cmd.sobel(image, result);', {buffers: {result: [320, 200]}, output: 'result'}),
  example('emboss', 'Effects', 'Emboss', 'Turn local changes into an embossed relief. Use a separate destination and an explicit strength.', 'cmd.emboss(image, result, {.strength = 1});', {buffers: {result: [320, 200]}, output: 'result'}),
  example('rounded_corners', 'Effects', 'Rounded corners', 'Radius is in pixels. The operation attenuates premultiplied RGB and alpha at the corners.', 'cmd.rounded_corners(image, {.radius = 36});'),
  example('vignette', 'Effects', 'Vignette', 'Darken the outer part of an image using normalized radius and softness. The vignette color is premultiplied.', 'cmd.vignette(image, {.radius = 0.5f, .softness = 0.4f, .color = {0, 0, 0, 0.85f}});'),
  example('chroma_key', 'Effects', 'Chroma key', 'Remove a key color with threshold, transition smoothness and spill suppression. This edits alpha and color, not a separate selection.', `cmd.fill(image, {.color = {0, 1, 0, 1}});
const std::array points{StrokeSample{{115, 100}}, StrokeSample{{160, 100}}, StrokeSample{{205, 100}}};
cmd.brush_stroke(image, {.samples = points, .brush = {.diameter = 96, .hardness = 0.8f, .spacing = 0}, .color = ${orange}});
cmd.chroma_key(image, {.key = {0, 1, 0, 1}, .threshold = 0.4f, .smoothness = 0.1f, .spill_suppression = 0.5f});`, {seed: false}),
  example('drop_shadow', 'Effects', 'Drop shadow', 'drop_shadow_requirements returns output dimensions and a workspace plan with blur padding. expand=false clips the result to the source size.', `cmd.circle(image, {.color = ${orange}, .background = {0, 0, 0, 0}, .softness = 2});
cmd.drop_shadow(image, result, options);`, {seed: false, setup: `DropShadowOptions options{
    .offset = {12, 10}, .radius = 10, .sigma = 4,
    .color = {0, 0, 0, 0.65f}, .expand = false,
};
const auto required = drop_shadow_requirements(image.size(), options);
options.workspace = ctx.create_workspace(required.workspace);`, buffers: {result: ['required.destination']}, output: 'result'}),
  example('blend', 'Compose', 'Blend a layer', 'Position uses destination pixels. All 28 modes are available; see Photoshop blend modes below. Opacity multiplies source coverage.', `cmd.circle(layer, {.color = ${orange}, .background = {0, 0, 0, 0}, .softness = 2});
cmd.blend(layer, image, {.position = {110, 45}, .opacity = 0.9f, .mode = BlendMode::screen});`, {buffers: {layer: [110, 110]}}),
  example('blend_many', 'Compose', 'Blend several layers', 'Layers are applied in array order. Batch consecutive pixel layers; keep adjustments and nested-document composition in your application.', `cmd.circle(first, {.color = ${orange}, .background = {0, 0, 0, 0}, .softness = 2});
cmd.polygon(second, {.sides = 5, .color = ${blue}, .background = {0, 0, 0, 0}, .rotation = 0, .softness = 2});
const std::array sources{first, second};
const std::array positions{Position{60, 45}, Position{145, 45}};
const std::array opacities{0.85f, 0.9f};
const std::array modes{BlendMode::normal, BlendMode::multiply};
cmd.blend_many(sources, image, {.positions = positions, .opacities = opacities, .modes = modes});`, {buffers: {first: [110, 110], second: [110, 110]}}),
  example( 'apply_mask', 'Compose', 'Apply a layer mask', 'Multiply premultiplied destination RGBA by A8 coverage. Position places the mask in destination coordinates; pixels outside it stay unchanged.', `cmd.circle(matte, {.color = {1, 1, 1, 1}, .background = {0, 0, 0, 0}, .softness = 8});
cmd.extract_mask(matte, coverage, {.mode = MaskMode::alpha});
cmd.apply_mask(coverage, image);`, {buffers : {matte : [ 320, 200 ]}, masks : {coverage : [ 320, 200 ]}}),
  example('region', 'Paint', 'Limit an edit to a rectangle', 'Regions use destination coordinates. An optional A8 coverage mask can further restrict the effect; initialize out-of-place destinations before masked operations.', `Rect area{40, 30, 150, 120};
cmd.grayscale(image, {.region = area});`, {covers: ['grayscale']}),

  // ---- compositing examples ----
  example('blend_modes', 'Compose', 'Photoshop blend modes', 'All 28 modes blend straight linear colors with source-over alpha. Here: hue, color dodge, vivid light and luminosity. Linear dodge saturates at the greater of one and the backdrop; add is unrestricted. Non-separable modes use W3C Lum/Sat/ClipColor.', `{
const std::array ramp{GradientStop{0, {0.8f, 0.02f, 0.1f, 1}}, GradientStop{1, {0.02f, 0.7f, 0.6f, 1}}};
cmd.gradient_fill(layer, {.start = {0, 0}, .end = {320, 200}, .stops = ramp,
    .interpolation = GradientInterpolation::linear, .dither = false, .replace = true});
}
const std::array sources{layer, layer, layer, layer};
const std::array positions{Position{0, 0}, Position{80, 0}, Position{160, 0}, Position{240, 0}};
const std::array modes{BlendMode::hue, BlendMode::color_dodge, BlendMode::vivid_light, BlendMode::luminosity};
cmd.blend_many(sources, image, {.positions = positions, .modes = modes});`, {buffers: {layer: [80, 200]}, covers: ['blend_many']}),
  example('layer_mask', 'Compose', 'A mask that moves with the layer', 'source_mask uses source coordinates and must match the layer size. The existing mask and region use destination coordinates. This circular source mask moves with each differently positioned layer.', `cmd.circle(shape, {.color = {1, 1, 1, 1}, .background = {0, 0, 0, 0}, .softness = 10});
cmd.extract_mask(shape, layerMask, {.mode = MaskMode::alpha});
{
const std::array ramp{GradientStop{0, ${orange}}, GradientStop{1, ${blue}}};
cmd.gradient_fill(layer, {.start = {0, 0}, .end = {320, 200}, .stops = ramp,
    .interpolation = GradientInterpolation::linear, .dither = false, .replace = true});
}
cmd.blend(layer, image, {.position = {30, 30}, .source_mask = &layerMask});
cmd.blend(layer, image, {.position = {170, 65}, .mode = BlendMode::screen, .source_mask = &layerMask});`, {setup: 'auto layerMask = ctx.create_mask({120, 120});', buffers: {shape: [120, 120], layer: [120, 120]}, covers: ['blend']}),
  example('clipping_group', 'Compose', 'Clipping group and alpha lock', 'Copy the base into an isolated group, then use preserve_alpha on each clipped layer. The base alpha stays fixed, even at soft edges. Composite the completed group onto the document. Keep the isolated base separate from the document backdrop.', `cmd.circle(base, {.color = ${orange}, .background = {0, 0, 0, 0}, .softness = 15});
cmd.copy(base, group);
cmd.stripes(layer, {.angle = 30, .spacing = 24, .width = 10, .first = ${blue}, .second = {0, 0, 0, 0}});
const std::array sources{layer};
const std::array positions{Position{}};
const std::array layers{BlendLayerOptions{.preserve_alpha = true}};
cmd.blend_many(sources, group, {.positions = positions, .layers = layers});
cmd.blend(group, image, {.position = {70, 10}});`, {buffers: {base: [180, 180], group: [180, 180], layer: [180, 180]}, covers: ['blend', 'blend_many']}),
  example( 'blend_if', 'Compose', 'Blend If with split sliders', 'With srgb encoding, 0.30/0.65 fades away this layer over dark underlying pixels. Divide sRGB-document 0–255 UI values by 255. The default encoding is linear. Source and destination ranges multiply source coverage before compositing.', `cmd.fill(layer, {.color = ${orange}});
cmd.blend(layer, image, {.opacity = 0.85f, .blend_if = BlendIfOptions{
    .destination = {.black = 0.30f, .black_split = 0.65f}, .encoding = ColorEncoding::srgb}});`, {buffers : {layer : [ 320, 200 ]}, covers : [ 'blend' ]}),
  example('dissolve', 'Compose', 'Seeded dissolve', 'Dissolve makes binary source-alpha decisions. Omitted seeds use source identity and dissolve order within a Commands recording, keeping the same stack stable on rerender. Set explicit per-layer seeds to preserve patterns across resource recreation or reordering. Its pattern uses source coordinates and moves with the layer. Destination coverage masks still soften the final write.', `cmd.circle(layer, {.color = ${orange}, .background = {0, 0, 0, 0}, .softness = 12});
cmd.blend(layer, image, {.position = {70, 10}, .opacity = 0.45f, .mode = BlendMode::dissolve, .seed = 42});`, {buffers: {layer: [180, 180]}, covers: ['blend']}),

  // ---- end compositing examples ----

  // ---- geometry examples ----
  example( 'projective_transform', 'Transform', 'Compose projective transforms', 'Homography matrices map continuous source points to destination points. Compose a perspective with an affine placement, then apply the same matrix to images and layer masks.', `const Homography perspective{{1, 0, 0, 0, 1, 0, 0.002, 0, 1}};
const ProjectiveTransformOptions options{.matrix = Homography::from(Affine::translate(40, 20)) * perspective};
cmd.transform(image, result, options);
cmd.select_ellipse(selection, {.origin = {20, 10}, .width = 280, .height = 180});
cmd.transform(selection, transformed, options);
cmd.apply_mask(transformed, result);`, { buffers : {result : [ 320, 200 ]}, masks : {selection : [ 320, 200 ], transformed : [ 320, 200 ]}, output : 'result', covers : [ 'transform:Image', 'transform:Mask' ] }),
  example('transform', 'Transform', 'Free transform', 'Photoshop Free Transform. The Affine maps source to destination pixels; positive degrees turn clockwise and the right operand applies first. transform_bounds gives the pixel rectangle the result covers: allocate it and translate by its origin. Reductions average each pixel’s footprint instead of aliasing; outside the source is transparent unless another EdgeMode (clamp, repeat, mirror) is chosen. Large reductions sample an area table, reduced source or pyramid in a workspace sized by transform_requirements.', 'cmd.transform(image, result, {.matrix = Affine::translate(-bounds.x, -bounds.y) * matrix, .filter = ResizeFilter::bicubic});', {setup: 'const Affine matrix = Affine::rotate(-20) * Affine::scale(0.75f, 0.5f);\nconst Rect bounds = transform_bounds({0, 0, 320, 200}, matrix);', buffers: {result: ['ImageSize{bounds.width, bounds.height}']}, output: 'result', covers: ['transform:Image']}),
  example('perspective', 'Transform', 'Perspective and distort', 'Photoshop Distort/Perspective: the destination positions of the source corners, in the order top-left, top-right, bottom-right, bottom-left, forming a convex quadrilateral. perspective_matrix returns the same homography for overlays and hit testing. The far side is filtered over its footprint, not point sampled. Steep foreshortening samples a box pyramid in a workspace sized by perspective_requirements.', 'const std::array corners{Point{95, 25}, Point{245, 10}, Point{315, 190}, Point{5, 175}};\ncmd.perspective(image, result, {.corners = corners, .filter = ResizeFilter::bilinear});', {buffers: {result: [320, 200]}, output: 'result', covers: ['perspective:Image']}),
  example('offset', 'Transform', 'Offset and wrap around', 'Photoshop Filter › Other › Offset moves by whole pixels. EdgeMode::repeat wraps around, useful to check seamless tiles; clamp repeats edge pixels, transparent clears and mirror reflects.', 'cmd.offset(image, result, {.offset = {160, 100}, .edge = EdgeMode::repeat});', {buffers: {result: [320, 200]}, output: 'result', covers: ['offset:Image']}),
  example('resize_reduction', 'Transform', 'Reduce without moiré', 'When the destination is smaller, bilinear, bicubic and lanczos widen with the reduction factor and area averages the exact pixel coverage, so fine detail becomes its average instead of moiré. nearest always point-samples.', `cmd.stripes(image, {.angle = 20, .spacing = 3, .width = 1.5f, .first = ${blue}, .second = ${paper}});
cmd.resize(image, result, {.filter = ResizeFilter::area});`, {seed: false, buffers: {result: [80, 50]}, output: 'result', covers: ['resize:Image']}),
  example('layer_mask_transform', 'Transform', 'Move a layer mask with its layer', 'Masks take the same geometry as images. Apply identical options to a layer and its mask so they stay aligned. Coverage is filtered like alpha and rounded to bytes.', `cmd.circle(matte, {.color = {1, 1, 1, 1}, .background = {0, 0, 0, 0}, .softness = 6});
cmd.extract_mask(matte, layer_mask, {.mode = MaskMode::alpha});
const TransformOptions move{.matrix = Affine::rotate(15, {160, 100}) * Affine::translate(50, 10),
                            .filter = ResizeFilter::bicubic};
cmd.transform(image, layer, move);
cmd.transform(layer_mask, moved_mask, move);
cmd.fill(result, {.color = ${paper}});
cmd.copy(layer, result, {.mask = &moved_mask});`, {buffers: {matte: [320, 200], layer: [320, 200], result: [320, 200]}, masks: {layer_mask: [320, 200], moved_mask: [320, 200]}, output: 'result', covers: ['transform:Mask']}),
  example( 'mask_geometry', 'Transform', 'Resize, crop, flip, rotate and offset masks', 'Every mask operation mirrors its image counterpart and writes a distinct destination mask; an optional selection mask and region limit which coverage changes. crop, flip, offset and nearest move bytes exactly.', `cmd.polygon(matte, {.sides = 3, .color = {1, 1, 1, 1}, .background = {0, 0, 0, 0}, .rotation = 0, .softness = 1});
cmd.extract_mask(matte, shape, {.mode = MaskMode::alpha});
cmd.zoom(shape, zoomed, {.factor = 1.1f, .center = {160, 100}});
cmd.copy(zoomed, shape);
cmd.resize(shape, small, {.filter = ResizeFilter::area});           // 160x100 triangle.
cmd.crop(small, placed, {.origin = {-10, -10}});                     // Placed at (10, 10).
cmd.flip(placed, mirrored, {.direction = FlipDirection::horizontal});
cmd.rotate(placed, turned, {.degrees = 180});                        // About the center.
cmd.offset(turned, lowered, {.offset = {-150, 0}, .edge = EdgeMode::transparent});
const std::array corners{Point{120, 40}, Point{200, 40}, Point{230, 190}, Point{90, 190}};
cmd.perspective(small, tilted, {.corners = corners});
cmd.fill(image, {.color = ${paper}});
cmd.fill(image, {.color = ${blue}, .mask = &placed});
cmd.fill(image, {.color = ${orange}, .mask = &mirrored});
cmd.fill(image, {.color = {0.015f, 0.22f, 0.13f, 1}, .mask = &lowered});
cmd.fill(image, {.color = {0, 0, 0, 0.6f}, .mask = &tilted});`, { seed : false, buffers : {matte : [ 320, 200 ]}, masks : { zoomed : [ 320, 200 ], shape : [ 320, 200 ], small : [ 160, 100 ], placed : [ 320, 200 ], mirrored : [ 320, 200 ], turned : [ 320, 200 ], lowered : [ 320, 200 ], tilted : [ 320, 200 ] }, covers : [ 'zoom:Mask', 'resize:Mask', 'crop:Mask', 'flip:Mask', 'rotate:Mask', 'offset:Mask', 'perspective:Mask' ] }),
  // ---- end geometry examples ----

  // ---- adjustments examples ----
  example('levels', 'Adjustments', 'Levels', 'Input/output endpoints use 0..1 instead of 0..255. Gamma is the familiar midtone value. Individual channels apply before the composite in signed extended sRGB. SDR input clips at the black/white endpoints; signed-power tails extend only existing negative/HDR input, continuously from 0/1.', `cmd.levels(image, {.composite = {.input_black = 0.12f, .input_white = 0.92f, .gamma = 1.3f}});`, {covers: ['levels:Image']}),
  example('curves', 'Adjustments', 'Curves', 'Strictly increasing input points define a shape-preserving cubic curve, sampled into a 4096-entry LUT. Points use 0..1 display coordinates; the curve is flat outside the control endpoints within 0..1, with tangent extensions only beyond 0..1. Empty channels are identity.', `const std::array points{Point{0, 0}, Point{0.25f, 0.12f},
                        Point{0.75f, 0.88f}, Point{1, 1}};
cmd.curves(image, {.composite = points});`),
  example('lut', 'Adjustments', 'One-dimensional LUT', 'Tables sample 0..1 uniformly, interpolate linearly and extrapolate using the endpoint segments; signed/HDR table values are supported. Individual channels apply before the composite; choose encoded sRGB or linear input/output.', `const std::array red{0.08f, 0.35f, 0.65f, 0.9f, 1.0f};
const std::array blue{0.0f, 0.18f, 0.38f, 0.65f, 0.9f};
cmd.lut(image, {.red = red, .blue = blue});`),
  example('lut3d', 'Adjustments', 'Three-dimensional LUT', 'RGB triplets use red-fastest cube order. Trilinear interpolation supports sizes 2..65, signed/HDR outputs and per-channel DOMAIN_MIN/MAX bounds. The consumer parses .cube files; extended sRGB and linear grading are supported.', `const std::array<float, 24> cube{
    0.03f, 0.01f, 0.08f,  1, 0.05f, 0,
    0.06f, 0.95f, 0.08f,  1, 0.9f, 0.05f,
    0, 0.05f, 0.85f,      0.95f, 0, 0.9f,
    0.05f, 0.9f, 0.9f,    1, 0.96f, 0.85f};
cmd.lut3d(image, {.size = 2, .values = cube,
                  .domain_min = {-0.1f, 0, 0}, .domain_max = {1.1f, 1, 1}});`),
  example('color_balance', 'Adjustments', 'Color balance', 'Cyan/red, magenta/green and yellow/blue controls use -1..1 for -100..100. Localized lightness bands separate shadows, midtones and highlights; correction is bounded to 0.7 encoded units and preserve luminosity restores encoded Rec.709 luma and compresses chroma into the input gamut, including its existing HDR range.', `cmd.color_balance(image, {.shadows = {-0.2f, 0.02f, 0.25f}, .highlights = {0.2f, 0.05f, -0.1f}});`),
  example('hue_saturation', 'Adjustments', 'Hue and saturation', 'Hue uses -180..180 degrees; saturation and lightness use -1..1 for -100..100. Saturation scales existing chroma continuously. Colorize accepts hue 0..360 and sets absolute HSL saturation in 0..1; active edits retain signed/HDR values using an expanded HSL interval.', `cmd.hue_saturation(image, {.hue = -25, .saturation = 0.3f, .lightness = 0.05f});`),
  example('color_matrix', 'Adjustments', 'RGBA color matrix', 'A row-major 4x5 matrix transforms straight RGBA and adds offsets. Both linear and extended sRGB domains support HDR and negative RGB; alpha is clamped and colors are re-premultiplied.', `cmd.color_matrix(image, {.matrix = {
    0.8f, 0.15f, 0.05f, 0, 0.02f,
    0.1f, 0.8f, 0.1f, 0, 0,
    0.15f, 0.2f, 0.65f, 0, 0,
    0, 0, 0, 1, 0}});`),
  example('channel_mixer', 'Adjustments', 'Channel mixer', 'Each row contains RGB coefficients and a constant. -2..2 maps to -200%..200%; monochrome uses the red output row for all channels. Mixing occurs in encoded sRGB.', `cmd.channel_mixer(image, {.red = {0.4f, 0.4f, 0.2f, 0}, .monochrome = true});`),
  example('black_white', 'Adjustments', 'Black and white', 'Six hue weights in red/yellow/green/cyan/blue/magenta order use -2..3 for -200%..300%. Optional tint takes its hue and saturation from an opaque linear color.', `cmd.black_white(image, {.weights = {0.6f, 0.8f, 0.4f, 0.6f, 0.2f, 0.7f}, .tint = true});`),
  example('photo_filter', 'Adjustments', 'Photo filter', 'Density 0..1 maps to 0%..100%. The opaque linear filter color multiplies encoded RGB; preserve luminosity restores luma and compresses chroma into the input gamut, expanded to retain existing signed/HDR values.', `cmd.photo_filter(image, {.color = {1, 0.35f, 0.06f, 1}, .density = 0.65f});`),
  example('posterize', 'Adjustments', 'Posterize', 'Clamp straight encoded RGB to 0..1 and quantize using 2..256 equal-width input bands and equally spaced output levels, then decode to linear and preserve alpha.', `cmd.posterize(image, {.levels = 4});`),
  example('gradient_map', 'Adjustments', 'Gradient map', 'Encoded RGB is clamped to 0..1; Rec.709 luma indexes ordered stops, with duplicates creating hard edges. Linear input stop colors are interpolated in encoded sRGB, so black-to-white maps preserve gray ramps. Stop alpha multiplies source alpha; use opaque stops for Photoshop-style maps.', `const std::array stops{GradientStop{0, {0.015f, 0.01f, 0.06f, 1}},
                       GradientStop{0.5f, {0.4f, 0.08f, 0.12f, 1}},
                       GradientStop{0.5f, {0.7f, 0.3f, 0.1f, 1}},
                       GradientStop{1, {1, 0.85f, 0.5f, 1}}};
cmd.gradient_map(image, {.stops = stops, .dither = true});`),
  // ---- end adjustments examples ----

  // ---- filters examples ----
  example('workspace', 'Use the library', 'Reuse temporary GPU memory', 'Requirements queries return plans without allocating GPU storage. Merge plans for sequential operations, reserve once, and pass the same Workspace in each operation’s options. Records keep it alive until submission retirement.', `cmd.box_blur(image, result, {.radius = 8, .workspace = workspace});
cmd.gaussian_blur(result, {.radius = 12, .sigma = 4, .workspace = workspace});`, {setup: `auto plan = box_blur_requirements(image.size(), {.radius = 8}).workspace;
plan.merge(gaussian_blur_requirements(image.size(), {.radius = 12, .sigma = 4}).workspace);
auto workspace = ctx.create_workspace(plan);`, buffers: {result: [320, 200]}, output: 'result', covers: ['box_blur', 'gaussian_blur']}),
  example('gaussian_blur', 'Filters', 'Gaussian blur', 'Radius is in pixels and sigma controls spread. gaussian_blur_requirements gives the workspace plan; the result replaces the input.', 'cmd.gaussian_blur(image, options);', {setup: `auto options = GaussianBlurOptions{.radius = 12, .sigma = 4};
options.workspace = ctx.create_workspace(gaussian_blur_requirements(image.size(), options).workspace);`}),
  example('box_blur', 'Filters', 'Box blur', 'Square box average with clamp edges; radius r uses a (2r+1)-pixel-wide square. Two separable passes reuse an explicitly reserved workspace.', 'cmd.box_blur(image, result, options);', {setup: `auto options = BoxBlurOptions{.radius = 8};
options.workspace = ctx.create_workspace(box_blur_requirements(image.size(), options).workspace);`, buffers: {result: [320, 200]}, output: 'result'}),
  example('motion_blur', 'Filters', 'Motion blur', 'Centered line exposure: clockwise angle in degrees and distance in pixels. Dense bilinear sampling, accelerated by dyadic line passes above 32 pixels.', 'cmd.motion_blur(image, result, options);', {setup: `auto options = MotionBlurOptions{.angle = 30, .distance = 96};
options.workspace = ctx.create_workspace(motion_blur_requirements(image.size(), options).workspace);`, buffers: {result: [320, 200]}, output: 'result'}),
  example('radial_blur', 'Filters', 'Radial blur', 'Spin sweeps an arc measured in degrees. Zoom sweeps inward by amount percent. Center uses continuous image coordinates. Long exposures use bounded rotation or scale passes.', 'cmd.radial_blur(image, result, options);', {setup: `auto options = RadialBlurOptions{.center = {160, 100}, .amount = 40, .mode = RadialBlurMode::spin};
options.workspace = ctx.create_workspace(radial_blur_requirements(image.size(), options).workspace);`, buffers: {result: [320, 200]}, output: 'result'}),
  example('unsharp_mask', 'Filters', 'Unsharp mask', 'Photoshop-style Amount percent, Gaussian radius in pixels and Threshold levels. Threshold compares sRGB-encoded channel levels; sharpening uses linear RGB; alpha is preserved. No output clipping.', 'cmd.unsharp_mask(image, result, options);', {setup: `auto options = UnsharpMaskOptions{.amount = 150, .radius = 3, .threshold = 2};
options.workspace = ctx.create_workspace(unsharp_mask_requirements(image.size(), options).workspace);`, buffers: {result: [320, 200]}, output: 'result'}),
  example('high_pass', 'Filters', 'High pass', 'Subtract the Gaussian low frequencies from straight RGB and add neutral gray 0.5 in linear light. Radius is Gaussian sigma; alpha is preserved.', 'cmd.high_pass(image, result, options);', {setup: `auto options = HighPassOptions{.radius = 6};
options.workspace = ctx.create_workspace(high_pass_requirements(image.size(), options).workspace);`, buffers: {result: [320, 200]}, output: 'result'}),
  example('median', 'Filters', 'Median', 'Exact component-wise median including alpha, radius up to 500. Small radii use sorting networks and large windows use tiled rank queries. median_requirements returns a reusable workspace plan. Reserve it explicitly before recording; small radii need no workspace.', 'cmd.median(image, result, {.radius = 5, .workspace = workspace});', {setup: `auto requirements = median_requirements(image.size(), {.radius = 5});
auto workspace = ctx.create_workspace(requirements.workspace);`, buffers: {result: [320, 200]}, output: 'result'}),
  example('minimum', 'Filters', 'Minimum', 'Expand dark regions using premultiplied RGBA minima, including alpha. Both shapes support radius 500. Square uses exact line extrema; large round supports approximate a disk with eight line directions.', 'cmd.minimum(image, result, options);', {setup: `auto options = MorphologyOptions{.radius = 3, .shape = MorphologyShape::round};
options.workspace = ctx.create_workspace(minimum_requirements(image.size(), options).workspace);`, buffers: {result: [320, 200]}, output: 'result'}),
  example('maximum', 'Filters', 'Maximum', 'Expand light regions using premultiplied RGBA maxima, including alpha. Reserve the plan returned by maximum_requirements before recording.', 'cmd.maximum(image, result, options);', {setup: `auto options = MorphologyOptions{.radius = 3, .shape = MorphologyShape::square};
options.workspace = ctx.create_workspace(maximum_requirements(image.size(), options).workspace);`, buffers: {result: [320, 200]}, output: 'result'}),
  example( 'pixelate', 'Filters', 'Pixelate', 'Mosaic averages premultiplied pixels in cells anchored at the image origin. Partial edge cells use only existing pixels; each cell is averaged once.', 'cmd.pixelate(image, result, {.cell_size = 16});', {buffers : {result : [ 320, 200 ]}, output : 'result', covers : [ 'pixelate' ]}),
  example('surface_blur', 'Filters', 'Surface blur', 'Bilateral smoothing with spatial sigma radius/2 and range sigma threshold/255 in straight linear RGB. Center alpha is preserved and transparent neighbors do not contribute. Threshold zero is identity. Large radii use multiscale separable bilateral passes.', 'cmd.surface_blur(image, result, options);', {setup: `auto options = SurfaceBlurOptions{.radius = 10, .threshold = 30};
options.workspace = ctx.create_workspace(surface_blur_requirements(image.size(), options).workspace);`, buffers: {result: [320, 200]}, output: 'result'}),
  example('add_noise', 'Filters', 'Add noise', 'Add reproducible uniform or Gaussian noise to straight linear RGB, preserving alpha. HDR and negative values are preserved by default; clip=true explicitly clamps RGB to [0,1]. Amount is percent: uniform half-width or Gaussian standard deviation. Monochromatic shares one sample across channels.', 'cmd.add_noise(image, {.amount = 8, .distribution = NoiseDistribution::gaussian, .monochrome = true, .seed = 42});'),
  example('sharpen', 'Filters', 'Sharpen', 'Neighborhood filters read a distinct source and overwrite the destination. Strength controls edge emphasis.', 'cmd.sharpen(image, result, {.strength = 1.5f});', {buffers: {result: [320, 200]}, output: 'result'}),
  // ---- end filters examples ----

  // ---- selection examples ----
  example( 'mask_edits', 'Select', 'Edit mask coverage locally', 'Mask copy, fill, invert, levels, threshold and combine accept a write mask and a clipped region. The selection blends old and computed coverage; untouched bytes keep their original values.', `cmd.select_ellipse(selection, {.origin = {30, 20}, .width = 260, .height = 160});
cmd.fill(control, {.coverage = 0.5f});
cmd.copy(selection, edited, {.region = Rect{0, 0, 200, 200}});
cmd.invert(edited, {.mask = &control, .region = Rect{140, 0, 180, 200}});
cmd.levels(edited, {.transfer = {.gamma = 1.8f}, .region = Rect{0, 0, 160, 200}});
cmd.threshold(edited, {.value = 0.4f, .region = Rect{0, 100, 320, 100}});
cmd.combine(selection, edited, {.mode = SelectionMode::intersect, .region = Rect{160, 0, 160, 200}});
cmd.apply_mask(edited, image);`, { masks : {selection : [ 320, 200 ], edited : [ 320, 200 ], control : [ 320, 200 ]}, covers : [ 'copy:Mask', 'invert:Mask', 'fill:Mask', 'levels:Mask', 'threshold:Mask', 'combine' ] }),
  example( 'mask_patch', 'Select', 'Transfer a mask patch', 'A8 transfers use tightly packed coverage bytes. Explicit buffer capacity and TransferOptions let undo patches and tiles transfer just their rectangle, including unaligned byte offsets.', `cmd.fill(selection, {.coverage = 0.2f});
cmd.upload(patch_upload, selection, {.region = Rect{73, 45, 95, 80}});
cmd.download(selection, saved, {.region = Rect{73, 45, 95, 80}});
cmd.apply_mask(selection, image);`, { setup : `auto selection = ctx.create_mask(image.size());
auto patch_upload = ctx.create_upload_buffer(selection, {.capacity_pixels = 95 * 80});
auto saved = ctx.create_readback_buffer(selection, {.capacity_pixels = 95 * 80});
std::array<std::uint8_t, 95 * 80> coverage{};
coverage.fill(255);
ctx.write(patch_upload, coverage);`, covers : [ 'upload:Mask', 'download:Mask' ] }),
  example('select_rectangle', 'Select', 'Rectangular marquee', 'Selections are A8 masks. Coordinates are continuous pixels: edges get exact area coverage, and corner_radius rounds the corners. Fill through the mask to see it; the selection limits any masked operation.', `cmd.select_rectangle(selection, {.origin = {40.5f, 30.25f}, .width = 170, .height = 120, .corner_radius = 28});
cmd.fill(image, {.color = {0.9f, 0.9f, 0.85f, 1}, .mask = &selection});`, {setup: `// Selections are A8 masks owned by the context; destroy them like images.
auto selection = ctx.create_mask(image.size());`, buffers: {}}),
  example('select_ellipse', 'Select', 'Elliptical marquee and modes', 'Modes match Photoshop: replace, add, subtract, intersect and difference, applied exactly to coverage bytes. transform rotates or skews any marquee (Transform Selection).', `cmd.select_ellipse(selection, {.origin = {30, 40}, .width = 150, .height = 120});
cmd.select_ellipse(selection, {.origin = {120, 40}, .width = 150, .height = 120,
                               .transform = Affine::rotate(30, {195, 100}), .mode = SelectionMode::difference});
cmd.fill(image, {.color = {0.9f, 0.9f, 0.85f, 1}, .mask = &selection});`, {setup: `// Selections are A8 masks owned by the context; destroy them like images.
auto selection = ctx.create_mask(image.size());`, buffers: {}}),
  example('select_polygon', 'Select', 'Lasso and feather', 'Points are closed contours (Lasso, Polygonal Lasso). Fill rules: nonzero or even_odd, which leaves this star’s center unselected. feather blurs the new shape in pixels, using a workspace sized by feather_requirements.', `const std::array star{Point{160, 12}, Point{215, 185}, Point{68, 78}, Point{252, 78}, Point{105, 185}};
cmd.select_polygon(selection, {.points = star, .rule = FillRule::even_odd, .feather = 3, .workspace = workspace});
cmd.fill(image, {.color = {0.9f, 0.9f, 0.85f, 1}, .mask = &selection});`, {setup: `// Selections are A8 masks owned by the context; destroy them like images.
auto selection = ctx.create_mask(image.size());
auto workspace = ctx.create_workspace(feather_requirements(image.size(), {.radius = 3}).workspace);`, buffers: {}}),
  example('combine', 'Select', 'Combine selections', 'Merge a saved selection or channel into another mask with a selection mode, like Load Selection.', `cmd.select_rectangle(selection, {.origin = {40, 40}, .width = 240, .height = 120});
cmd.select_ellipse(saved, {.origin = {100, 20}, .width = 120, .height = 160});
cmd.combine(saved, selection, {.mode = SelectionMode::subtract});
cmd.fill(image, {.color = {0.9f, 0.9f, 0.85f, 1}, .mask = &selection});`, {setup: `// Selections are A8 masks owned by the context; destroy them like images.
auto selection = ctx.create_mask(image.size());
auto saved = ctx.create_mask(image.size());`, buffers: {}}),
  example('feather', 'Select', 'Feather', 'Select > Modify > Feather: a Gaussian blur of the selection; radius is the standard deviation in pixels. canvas_bounds treats the outside of the canvas as unselected.', `cmd.select_rectangle(selection, {.origin = {60, 40}, .width = 200, .height = 120});
cmd.feather(selection, {.radius = 12, .workspace = workspace});
cmd.fill(image, {.color = {0.9f, 0.9f, 0.85f, 1}, .mask = &selection});`, {setup: `// Selections are A8 masks owned by the context; destroy them like images.
auto selection = ctx.create_mask(image.size());
auto workspace = ctx.create_workspace(feather_requirements(image.size(), {.radius = 12}).workspace);`, buffers: {}}),
  example('expand', 'Select', 'Expand and contract', 'Select > Modify > Expand and Contract use exact Euclidean distances, so corners round and edges stay anti-aliased. Expand never removes and contract never adds coverage.', `const std::array stroke{Point{40, 150}, Point{120, 40}, Point{190, 150}, Point{280, 50}};
cmd.select_polygon(selection, {.points = stroke});
cmd.expand(selection, {.radius = 14, .workspace = workspace});
cmd.fill(image, {.color = {0.9f, 0.9f, 0.85f, 1}, .mask = &selection});
cmd.contract(selection, {.radius = 24, .workspace = workspace});
cmd.fill(image, {.color = {0.6f, 0.05f, 0.05f, 1}, .mask = &selection});`, {setup: `// Selections are A8 masks owned by the context; destroy them like images.
auto selection = ctx.create_mask(image.size());
auto workspace = ctx.create_workspace(expand_requirements(image.size(), {.radius = 24}).workspace);`, buffers: {}, covers: ['expand', 'contract']}),
  example('border', 'Select', 'Border', 'Select > Modify > Border: an anti-aliased band of the given total width centered on the selection edge.', `cmd.select_ellipse(selection, {.origin = {70, 30}, .width = 180, .height = 140});
cmd.border(selection, {.width = 16, .workspace = workspace});
cmd.fill(image, {.color = {0.9f, 0.9f, 0.85f, 1}, .mask = &selection});`, {setup: `// Selections are A8 masks owned by the context; destroy them like images.
auto selection = ctx.create_mask(image.size());
auto workspace = ctx.create_workspace(border_requirements(image.size(), {.width = 16}).workspace);`, buffers: {}}),
  example( 'smooth', 'Select', 'Smooth', 'Select > Modify > Smooth: majority vote over a square of the given radius removes specks and rounds jagged steps.', `cmd.select_color_range(image, selection, {.color = {0.95f, 0.24f, 0.055f, 1}, .fuzziness = 60.0f / (255.0f * 1.7320508f)});
cmd.smooth(selection, {.radius = 8, .workspace = workspace});
cmd.fill(image, {.color = {0.9f, 0.9f, 0.85f, 1}, .mask = &selection});`, { setup : `// Selections are A8 masks owned by the context; destroy them like images.
auto selection = ctx.create_mask(image.size());
auto workspace = ctx.create_workspace(smooth_requirements(image.size(), {.radius = 8}).workspace);`, buffers : {} }),
  example( 'mask_levels', 'Select', 'Threshold and levels', 'Remap selection coverage like Levels in Quick Mask; threshold makes a hard selection. Values are coverage in [0, 1].', `cmd.select_ellipse(selection, {.origin = {40, 20}, .width = 240, .height = 160});
cmd.feather(selection, {.radius = 30, .workspace = workspace});
cmd.levels(selection, {.transfer = {.input_black = 0.3f, .input_white = 0.9f, .gamma = 1.5f}});
cmd.fill(image, {.color = {0.9f, 0.9f, 0.85f, 1}, .mask = &selection});
cmd.threshold(selection, {.value = 0.95f});
cmd.fill(image, {.color = {0.6f, 0.05f, 0.05f, 1}, .mask = &selection});`, { setup : `// Selections are A8 masks owned by the context; destroy them like images.
auto selection = ctx.create_mask(image.size());
auto workspace = ctx.create_workspace(feather_requirements(image.size(), {.radius = 30}).workspace);`, buffers : {}, covers : [ 'levels:Mask', 'threshold:Mask' ] }),
  example( 'select_color_range', 'Select', 'Color range', 'Select > Color Range: coverage falls linearly with straight sRGB distance normalized by the RGB cube diagonal from the color, reaching zero at fuzziness, and scales with pixel alpha. The color is linear premultiplied like pixels.', `cmd.select_color_range(image, selection, {.color = {0.95f, 0.24f, 0.055f, 1}, .fuzziness = 80.0f / (255.0f * 1.7320508f)});
cmd.invert(selection);
cmd.grayscale(image, {.mask = &selection});`, { setup : `// Selections are A8 masks owned by the context; destroy them like images.
auto selection = ctx.create_mask(image.size());`, buffers : {} }),
  example( 'select_magic_wand', 'Select', 'Magic wand', 'Contiguous selection by normalized per-channel tolerance in [0,1] on rounded sRGB and alpha, exact in one submission (union-find, no readback). Turn contiguous off to select every matching pixel; sample_radius averages the reference; anti_alias softens edges.', `cmd.select_magic_wand(image, selection, {.seed = {80, 100}, .tolerance = 40.0f / 255.0f, .sample_radius = 1, .workspace = workspace});
cmd.fill(image, {.color = {0.9f, 0.9f, 0.85f, 1}, .mask = &selection});`, { setup : `// Selections are A8 masks owned by the context; destroy them like images.
auto selection = ctx.create_mask(image.size());
auto workspace = ctx.create_workspace(select_magic_wand_requirements(image.size(), {}).workspace);`, buffers : {} }),
  // ---- end selection examples ----

  // ---- painting examples ----
  example('brush_stroke', 'Paint', 'Brush tool', 'Samples are pointer events: image position, pressure, tilt and rotation. The engine places dabs every spacing × diameter along the path; minimum_size, minimum_opacity and minimum_flow map pressure like Photoshop’s Pen Pressure controls. Opacity caps the whole stroke, flow sets how fast overlapping dabs build up. brush_dabs and stroke_bounds return the dabs and the dirty rectangle for history. Set brush.tip to a Mask for sampled tips.', `const std::array samples{StrokeSample{{40, 160}, 0.15f}, StrokeSample{{110, 60}, 0.8f},
                         StrokeSample{{190, 150}, 1.0f}, StrokeSample{{285, 45}, 0.3f}};
const Brush brush{.diameter = 34, .hardness = 0.7f, .spacing = 0.08f, .minimum_size = 0.15f};
cmd.brush_stroke(image, {.samples = samples, .brush = brush, .color = {0.9f, 0.86f, 0.78f, 1},
                         .opacity = 0.9f, .flow = 0.6f});`, {covers: ['brush_stroke:Image']}),
  example('eraser_stroke', 'Paint', 'Eraser tool', 'Scales color and alpha toward transparency with the same brush engine, opacity cap and flow. Save as PNG to see the transparent result.', `const std::array samples{StrokeSample{{30, 100}}, StrokeSample{{160, 60}}, StrokeSample{{290, 110}}};
cmd.eraser_stroke(image, {.samples = samples, .brush = {.diameter = 46, .hardness = 0.3f}, .opacity = 0.85f});`, {covers: ['eraser_stroke:Image']}),
  example('clone_stroke', 'Paint', 'Clone stamp', 'Paints source pixels found at destination − offset; the offset is the destination minus the Alt-clicked source point. Keep one offset for Aligned, recompute it per stroke otherwise. Clone within one layer from a copy.', `cmd.copy(image, source);
const std::array samples{StrokeSample{{225, 60}}, StrokeSample{{225, 170}}};
cmd.clone_stroke(source, image, {.samples = samples, .brush = {.diameter = 70, .hardness = 0.5f},
                                 .offset = {140, 0}});`, {buffers: {source: [320, 200]}}),
  example('pattern_stroke', 'Paint', 'Pattern stamp', 'Paints a tiled pattern through an Affine that maps pattern to image coordinates. Aligned keeps one transform; non-aligned translates it to each stroke’s start.', `cmd.checkerboard(tile, {.size = 8, .first = ${blue}, .second = ${paper}});
const std::array samples{StrokeSample{{40, 150}}, StrokeSample{{160, 40}}, StrokeSample{{280, 150}}};
cmd.pattern_stroke(tile, image, {.samples = samples, .brush = {.diameter = 40, .hardness = 0.8f},
                                 .transform = Affine::rotate(45), .opacity = 0.9f});`, {buffers: {tile: [16, 16]}}),
  example('dodge_burn_stroke', 'Paint', 'Dodge and burn', 'Photoshop Range (shadows, midtones, highlights) and Exposure. Exposure caps the stroke like an opacity. Protect Tones moves luminance and keeps hue.', `const std::array top{StrokeSample{{20, 60}}, StrokeSample{{300, 60}}};
const std::array bottom{StrokeSample{{20, 150}}, StrokeSample{{300, 150}}};
const Brush brush{.diameter = 60, .hardness = 0};
cmd.dodge_burn_stroke(image, {.samples = top, .brush = brush, .range = ToneRange::midtones, .exposure = 0.8f});
cmd.dodge_burn_stroke(image, {.samples = bottom, .brush = brush, .burn = true, .range = ToneRange::midtones, .exposure = 0.8f});`),
  example('sponge_stroke', 'Paint', 'Sponge', 'Desaturates toward linear luminance or saturates away from it. Flow builds the effect up; vibrance protects already saturated colors.', `const std::array samples{StrokeSample{{40, 100}}, StrokeSample{{280, 100}}};
cmd.sponge_stroke(image, {.samples = samples, .brush = {.diameter = 90, .hardness = 0.2f}, .flow = 0.9f});`),
  example('focus_stroke', 'Paint', 'Blur and sharpen tools', 'Mixes toward a 5×5 blur, or away from it for sharpen, by the stroke coverage; strength caps it. The operation copies the stroke area into a workspace plane reserved before recording.', `const std::array samples{StrokeSample{{50, 40}}, StrokeSample{{140, 160}}, StrokeSample{{280, 80}}};
cmd.focus_stroke(image, {.samples = samples, .brush = {.diameter = 44, .hardness = 0.4f}, .strength = 1, .workspace = workspace});`, {setup: 'auto workspace = ctx.create_workspace(focus_stroke_requirements(image.size()).workspace);'}),
  example('smudge_stroke', 'Paint', 'Smudge', 'Dabs run in order: the first picks up the pixels under it and every later dab mixes what it carries with the image before depositing it. Strength 1 drags the first colors along the whole stroke. smudge_stroke_requirements reports the workspace capacity for the carried patch.', `const std::array samples{StrokeSample{{80, 100}}, StrokeSample{{150, 90}}, StrokeSample{{240, 130}}};
cmd.smudge_stroke(image, {.samples = samples, .brush = brush, .strength = 0.85f, .workspace = workspace});`, {setup: 'const Brush brush{.diameter = 44, .hardness = 0.5f, .spacing = 0.05f};\nauto workspace = ctx.create_workspace(smudge_stroke_requirements(image.size(), {.brush = brush}).workspace);', covers: ['smudge_stroke:Image']}),
  example('gradient_fill', 'Paint', 'Gradient tool', 'Drag from start to end. Shapes: linear, radial, angle, reflected and diamond; stops are premultiplied linear colors at nondecreasing positions. Perceptual interpolation mixes sRGB-encoded colors like Photoshop; dither prevents banding in 8-bit output. extend clamps, repeats, mirrors or leaves pixels unchanged.', `const std::array stops{GradientStop{0, ${blue}}, GradientStop{0.55f, ${paper}},
                       GradientStop{0.55f, {0.4f, 0.1f, 0.02f, 0.8f}}, GradientStop{1, ${orange}}};
cmd.gradient_fill(image, {.start = {160, 100}, .end = {290, 60}, .stops = stops,
                          .shape = GradientShape::angle, .dither = true});`, {seed: false}),
  example('pattern_fill', 'Paint', 'Pattern fill', 'Tiles a pattern image over the destination through an Affine (pattern to image), with bilinear filtering, a blend mode and opacity.', `cmd.stripes(tile, {.angle = 0, .spacing = 12, .width = 5, .first = ${orange}, .second = {0, 0, 0, 0}});
cmd.pattern_fill(tile, image, {.transform = Affine::rotate(30) * Affine::scale(1.5f), .mode = BlendMode::multiply, .opacity = 0.8f});`, {buffers: {tile: [24, 24]}}),
  example('paint_bucket', 'Paint', 'Paint bucket', 'Fills every pixel within tolerance of the seed pixel’s original color; 32/255 matches Photoshop’s default. This is non-contiguous; for Contiguous pass a flood-fill selection mask and set match_seed to false.', `cmd.paint_bucket(image, {.seed = {80, 100}, .tolerance = 0.2f, .softness = 0.05f, .color = ${blue}});`),
  example('mask_brush', 'Paint', 'Paint a layer mask', 'Brush coverage paints toward a mask value; the eraser paints toward zero. Opacity, flow, tips, selection and region work on A8 masks. The result below reveals the painted mask as a color.', `cmd.fill(selection, {.coverage = 0});
const std::array stroke{StrokeSample{{40, 150}}, StrokeSample{{160, 40}}, StrokeSample{{280, 150}}};
cmd.brush_stroke(selection, {.samples = stroke, .brush = {.diameter = 45, .hardness = 0.6f}, .coverage = 1});
const std::array cut{StrokeSample{{160, 30}}, StrokeSample{{160, 170}}};
cmd.eraser_stroke(selection, {.samples = cut, .brush = {.diameter = 25}, .opacity = 0.8f});
cmd.fill(image, {.color = ${orange}, .mask = &selection});`, {setup: 'auto selection = ctx.create_mask(image.size());', covers: ['brush_stroke:Mask', 'eraser_stroke:Mask']}),
  example('stroke_continuation', 'Paint', 'Continue a stroke', 'Reserve the snapshot explicitly, then pass only new samples to the same move-only state. reset() starts another stroke without reallocating the snapshot. It retains the original pixels and accumulated events, replaying them exactly so spacing, random dabs and opacity agree with one call. Keep options and selection fixed. Replay costs grow with the whole stroke.', `const Brush brush{.diameter = 35, .hardness = 0.6f, .spacing = 0.08f, .size_jitter = 0.3f, .seed = 17};
const std::array first{StrokeSample{{30, 140}}, StrokeSample{{130, 60}}};
const std::array next{StrokeSample{{215, 150}}, StrokeSample{{290, 50}}};
cmd.brush_stroke(image, state, {.samples = first, .brush = brush, .color = ${orange}, .opacity = 0.7f});
cmd.brush_stroke(image, state, {.samples = next, .brush = brush, .color = ${orange}, .opacity = 0.7f});`, {setup: 'auto state = ctx.create_brush_stroke_state(image);', covers: ['brush_stroke:Image']}),
  example('mask_continuation', 'Paint', 'Continue mask strokes', 'Each state represents one stroke. Mask continuation replays before A8 rounding, so segment boundaries cannot accumulate rounding errors.', `cmd.fill(selection, {.coverage = 0});
const std::array first{StrokeSample{{35, 140}}, StrokeSample{{145, 50}}};
const std::array next{StrokeSample{{285, 150}}};
const Brush brush{.diameter = 45, .hardness = 0.6f};
cmd.brush_stroke(selection, paint, {.samples = first, .brush = brush, .coverage = 1, .opacity = 0.8f});
cmd.brush_stroke(selection, paint, {.samples = next, .brush = brush, .coverage = 1, .opacity = 0.8f});
cmd.eraser_stroke(selection, erase, {.samples = first, .brush = {.diameter = 15}, .opacity = 0.5f});
cmd.eraser_stroke(selection, erase, {.samples = next, .brush = {.diameter = 15}, .opacity = 0.5f});
cmd.fill(image, {.color = ${orange}, .mask = &selection});`, {setup: 'auto selection = ctx.create_mask(image.size());\nauto paint = ctx.create_brush_stroke_state(selection);\nauto erase = ctx.create_brush_stroke_state(selection);', covers: ['brush_stroke:Mask', 'eraser_stroke:Mask']}),
  example('eraser_continuation', 'Paint', 'Continue erasing', 'Keep an eraser stroke’s opacity cap across calls with a BrushStrokeState.', `const std::array first{StrokeSample{{30, 100}}, StrokeSample{{155, 60}}};
const std::array next{StrokeSample{{290, 120}}};
cmd.eraser_stroke(image, state, {.samples = first, .brush = {.diameter = 42}, .opacity = 0.7f});
cmd.eraser_stroke(image, state, {.samples = next, .brush = {.diameter = 42}, .opacity = 0.7f});`, {setup: 'auto state = ctx.create_brush_stroke_state(image);', covers: ['eraser_stroke:Image']}),
  example('smudge_continuation', 'Paint', 'Continue smudging', 'SmudgeStrokeState preserves the complete stroke result, including pigment transport. Reserve the state snapshot and the transient workspace before recording. Reset keeps the snapshot capacity for the next stroke.', `const std::array first{StrokeSample{{70, 100}}, StrokeSample{{150, 70}}};
const std::array next{StrokeSample{{270, 140}}};
cmd.smudge_stroke(image, state, {.samples = first, .brush = brush, .strength = 0.9f, .workspace = workspace});
cmd.smudge_stroke(image, state, {.samples = next, .brush = brush, .strength = 0.9f, .workspace = workspace});`, {setup: 'const Brush brush{.diameter = 45, .hardness = 0.5f};\nauto state = ctx.create_smudge_stroke_state(image);\nauto workspace = ctx.create_workspace(smudge_stroke_requirements(image.size(), {.brush = brush}).workspace);', covers: ['smudge_stroke:Image']}),
  example('gradient_replace', 'Create', 'Replace with a gradient', 'Replace writes the ramp including transparent stops. Opacity, mask and region interpolate it with the previous destination. Start and end use image coordinates.', `const std::array ramp{GradientStop{0, ${blue}}, GradientStop{1, {0, 0, 0, 0}}};
cmd.gradient_fill(image, {.start = {30, 100}, .end = {290, 100}, .stops = ramp,
    .interpolation = GradientInterpolation::linear, .dither = false, .replace = true});`, {covers: ['gradient_fill']}),
  example('painting_alpha_lock', 'Paint', 'Lock transparent pixels', 'preserve_alpha changes color while retaining destination alpha. Transparent pixels stay transparent. Brush, clone, pattern, gradient, bucket, focus and smudge tools support it.', `cmd.circle(image, {.color = ${blue}, .background = {0, 0, 0, 0}, .softness = 20});
const std::array stroke{StrokeSample{{20, 120}}, StrokeSample{{300, 50}}};
cmd.brush_stroke(image, {.samples = stroke, .brush = {.diameter = 55}, .color = ${orange}, .preserve_alpha = true});`, {seed: false, covers: ['brush_stroke:Image']}),
  example('bucket_selection', 'Paint', 'Fill a contiguous selection', 'Set match_seed to false to fill exactly through a wand selection, including its antialiased edge. Seed, tolerance and softness are then ignored.', `cmd.select_magic_wand(image, selection, {.seed = {80, 100}, .tolerance = 40.0f / 255.0f, .workspace = workspace});
cmd.paint_bucket(image, {.color = ${orange}, .match_seed = false, .mask = &selection});`, {setup: 'auto selection = ctx.create_mask(image.size());\nauto workspace = ctx.create_workspace(select_magic_wand_requirements(image.size(), {}).workspace);', covers: ['paint_bucket']}),
  // ---- end painting examples ----

  // ---- analysis examples ----
  example( 'histogram', 'Analyze', 'Histogram', 'Count red, green, blue, alpha and luminosity levels on the GPU. The default sRGB space shows the levels Photoshop’s Histogram panel shows; 256 bins hold exactly the 8-bit levels of an RGBA8 download, and up to 4096 bins suit 16-bit views. A mask counts pixels at least half selected; a region limits the area. Read the counts after the batch completes.', `auto histogram = ctx.create_histogram_buffer(); // 256 bins: one per 8-bit level.
cmd.histogram(image, histogram, {.space = ColorEncoding::srgb});
// After the batch: rows of 256 counts for red, green, blue, alpha, luminosity.
std::vector<std::uint32_t> counts(histogram.bins() * analysis_channel_count);
ctx.read(histogram, counts);`, { code : String.raw`#include <wgpupixel_io.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

using namespace wgpupixel;

int main() {
    auto ctx = Context::create();
    auto image = ctx.create_image({320, 200});
    auto histogram = ctx.create_histogram_buffer(); // 256 bins: one per 8-bit level.

    ctx.run_and_wait([&](Commands& cmd) {
        {
const std::array ramp{GradientStop{0, {0.025f, 0.16f, 0.42f, 1}}, GradientStop{1, {0.88f, 0.85f, 0.77f, 1}}};
cmd.gradient_fill(image, {.start = {0, 0}, .end = {320, 200}, .stops = ramp,
    .interpolation = GradientInterpolation::linear, .dither = false, .replace = true});
}
        cmd.fill(image, {.color = {0.95f, 0.24f, 0.055f, 1}, .region = Rect{36, 40, 105, 120}});
        // Count sRGB levels, as Photoshop's Histogram panel does.
        cmd.histogram(image, histogram, {.space = ColorEncoding::srgb});
    });
    std::vector<std::uint32_t> counts(histogram.bins() * analysis_channel_count);
    ctx.read(histogram, counts); // Rows: red, green, blue, alpha, luminosity.

    // Chart red, green and blue added together, like the panel's Colors view. Square roots
    // keep the tall spikes of flat areas from hiding the gradient.
    auto chart = ctx.create_image({320, 200});
    auto layer = ctx.create_image({320, 200});
    const auto peak = *std::max_element(counts.begin(), counts.begin() + 3 * 256);
    const std::array colors{Color{0.9f, 0.02f, 0.02f, 1}, Color{0.02f, 0.9f, 0.02f, 1},
                            Color{0.02f, 0.05f, 0.9f, 1}};
    ctx.run_and_wait([&](Commands& cmd) {
        cmd.fill(chart, {.color = {0.012f, 0.012f, 0.012f, 1}});
        for (std::size_t channel = 0; channel < 3; ++channel) {
            cmd.fill(layer, {.color = {0, 0, 0, 0}});
            for (std::int32_t level = 0; level < 256; ++level) {
                const auto count = counts[channel * 256 + level];
                const auto height = std::int32_t(std::lround(170 * std::sqrt(double(count) / peak)));
                if (height > 0) {
                    cmd.fill(layer, {.color = colors[channel], .region = Rect{32 + level, 185 - height, 1, height}});
                }
            }
            cmd.blend(layer, chart, {.mode = BlendMode::add});
        }
    });

    io::save(ctx, chart, "output.png"); // Replaces this file.
    ctx.destroy(layer);
    ctx.destroy(chart);
    ctx.destroy(histogram);
    ctx.destroy(image);
}
` }),
  example('statistics', 'Analyze', 'Statistics and eyedropper', 'Minimum, maximum, mean and standard deviation per channel, reduced in double precision. The default sRGB space matches Photoshop’s Info panel levels; linear keeps HDR values. sample_region gives the eyedropper’s Sample Size square (1, 3, 5 … 101); ImageStatistics::average is the sampled linear premultiplied color, ready to use as a fill color. The swatches show the image average and a 5 by 5 sample.', `auto statistics = ctx.create_statistics_buffer();
auto sample = ctx.create_statistics_buffer();
cmd.statistics(image, statistics); // Whole image; a mask or region narrows it.
// Eyedropper, "5 by 5 Average" under the cursor:
cmd.statistics(image, sample, {.region = sample_region({60.5f, 90.5f}, 5)});
// After the batch:
const auto whole = ctx.read(statistics); // whole.luminosity.mean, whole.red.minimum, ...
const auto picked = ctx.read(sample).average;`, {code: String.raw`#include <wgpupixel_io.h>
#include <array>
#include <iostream>

using namespace wgpupixel;

int main() {
    auto ctx = Context::create();
    auto image = ctx.create_image({320, 200});
    auto statistics = ctx.create_statistics_buffer();
    auto sample = ctx.create_statistics_buffer();

    ctx.run_and_wait([&](Commands& cmd) {
        {
const std::array ramp{GradientStop{0, {0.025f, 0.16f, 0.42f, 1}}, GradientStop{1, {0.88f, 0.85f, 0.77f, 1}}};
cmd.gradient_fill(image, {.start = {0, 0}, .end = {320, 200}, .stops = ramp,
    .interpolation = GradientInterpolation::linear, .dither = false, .replace = true});
}
        cmd.fill(image, {.color = {0.95f, 0.24f, 0.055f, 1}, .region = Rect{36, 40, 105, 120}});
        const std::array dots{StrokeSample{{220, 95}}};
        cmd.brush_stroke(image, {.samples = dots, .brush = {.diameter = 108, .hardness = 0.95f, .spacing = 0}, .color = {0.015f, 0.22f, 0.13f, 1}});
        cmd.statistics(image, statistics); // Whole image; a mask or region narrows it.
        // Eyedropper "5 by 5 Average" under the cursor at (60.5, 90.5).
        cmd.statistics(image, sample, {.region = sample_region({60.5f, 90.5f}, 5)});
    });
    const auto whole = ctx.read(statistics);
    const auto picked = ctx.read(sample);
    std::cout << whole.pixels << " pixels; luminosity mean " << whole.luminosity.mean * 255
              << ", deviation " << whole.luminosity.deviation * 255 << " (8-bit levels); red "
              << whole.red.minimum * 255 << ".." << whole.red.maximum * 255 << '\n';

    // Swatches: the average color of the image and the eyedropper sample.
    ctx.run_and_wait([&](Commands& cmd) {
        cmd.fill(image, {.color = {1, 1, 1, 1}, .region = Rect{242, 12, 66, 176}});
        cmd.fill(image, {.color = whole.average, .region = Rect{248, 18, 54, 79}});
        cmd.fill(image, {.color = picked.average, .region = Rect{248, 103, 54, 79}});
    });

    io::save(ctx, image, "output.png"); // Replaces this file.
    ctx.destroy(sample);
    ctx.destroy(statistics);
    ctx.destroy(image);
}
`}),
  {id: 'viewport', group: 'Use the library', title: 'Present an editor viewport', description: 'Draw a pan/zoom/rotate view of the canvas for an editor: nearest pixels at 100% and above, area-averaged pyramid levels when zoomed out, a transparency checkerboard, the pasteboard color, a Quick Mask style selection overlay and a pixel grid above 500%. Presenter and Display accept the same ViewportOptions.', covers: ['ViewportOptions'], code: String.raw`#include <wgpupixel_webgpu.h>

using namespace wgpupixel;

int main() {
    auto ctx = Context::create();
    auto canvas = ctx.create_image({4000, 3000});
    auto shape = ctx.create_image({4000, 3000});
    auto selection = ctx.create_mask({4000, 3000});
    auto cmd = ctx.create_commands();
    cmd.fill(canvas, {.color = {0, 0, 0, 0}}); // Transparent: the checkerboard shows.
    cmd.fill(canvas, {.color = {0.2f, 0.05f, 0.01f, 0.5f}, .region = Rect{500, 400, 2000, 1500}});
    cmd.circle(shape, {.color = {1, 1, 1, 1}, .background = {0, 0, 0, 0}, .softness = 40});
    cmd.extract_mask(shape, selection, {.mode = MaskMode::alpha}); // A soft round selection.
    ctx.submit_and_wait(cmd);
    ctx.destroy(shape);

    // A 1280 x 720 texture your UI samples; the Display keeps its view alive.
    auto display = webgpu::Display::create(ctx, 1280, 720);
    // Fit the canvas (zoom 24%), centered; view maps canvas pixels to display pixels.
    const float zoom = 0.24f;
    webgpu::ViewportOptions view{
        .view = Affine::translate((1280 - 4000 * zoom) / 2, (720 - 3000 * zoom) / 2) *
                Affine::scale(zoom),
        .overlay = &selection, // Tints unselected areas red at 50%, like Quick Mask.
    };
    display.reserve(canvas.size(), view); // Explicit cache reservation; draw never grows it.
    display.draw(canvas, view);
    display.wait();

    // Zoom to 800% around canvas point (1000, 800), rotated 15 degrees, with a pixel grid.
    view.view = Affine::translate(640, 360) * Affine::rotate(15) * Affine::scale(8) *
                Affine::translate(-1000, -800);
    view.overlay = nullptr;
    view.pixel_grid = true;
    display.reserve(canvas.size(), view); // Explicit cache reservation; draw never grows it.
    display.draw(canvas, view);
    display.wait(); // Wait before editing the canvas again.
    display.close();
    ctx.destroy(selection);
    ctx.destroy(canvas);
}
`},
  // ---- end analysis examples ----
];

const seed = `{
const std::array ramp{GradientStop{0, {0.025f, 0.16f, 0.42f, 1}}, GradientStop{1, {0.88f, 0.85f, 0.77f, 1}}};
cmd.gradient_fill(image, {.start = {0, 0}, .end = {320, 200}, .stops = ramp,
    .interpolation = GradientInterpolation::linear, .dither = false, .replace = true});
}
Rect patch{36, 40, 105, 120};
cmd.fill(image, {.color = {0.95f, 0.24f, 0.055f, 1}, .region = patch});
const std::array dots{StrokeSample{{220, 95}}};
cmd.brush_stroke(image, {.samples = dots, .brush = {.diameter = 108, .hardness = 0.95f, .spacing = 0}, .color = {0.015f, 0.22f, 0.13f, 1}});`;

export function sourceOf(item) {
  if (item.code) return item.code.trim() + '\n';
  const buffers = {image: [320, 200], ...item.buffers};
  const allocations = Object.entries(buffers).flatMap(([name, size]) => [
    `auto ${name} = ctx.create_image(${size.length === 2 ? '{' + size.join(', ') + '}' : size[0]});`,
    ...(name === 'image' && item.setup ? [item.setup] : [])
  ]);
  const masks = Object.entries(item.masks ?? {}).map(([name, size]) =>
    `auto ${name} = ctx.create_mask({${size.join(', ')}});`);
  const recording = [...(item.seed === false ? [] : [seed, '']),
    '// Apply the operation.', item.body].join('\n');
  const lines = [...allocations, ...masks, '', 'ctx.run_and_wait([&](Commands& cmd) {',
    ...recording.split('\n').map(line => line ? '    ' + line : ''), '});', '',
    `io::save(ctx, ${item.output ?? 'image'}, "output.png"); // Replaces this file.`, '',
    ...[...Object.keys(buffers), ...Object.keys(item.masks ?? {})].reverse().map(name => `ctx.destroy(${name});`)];
  return `#include <wgpupixel_io.h>\n#include <array>\n\nusing namespace wgpupixel;\n\nint main() {\n    auto ctx = Context::create();\n${lines.join('\n').split('\n').map(line => line ? '    ' + line : '').join('\n')}\n}\n`;
}

export function operationOf(item) {
  if (!item.setup) return item.body?.trim() ?? sourceOf(item);
  const allocations = Object.entries(item.buffers ?? {}).map(([name, size]) =>
    `auto ${name} = ctx.create_image(${size.length === 2 ? '{' + size.join(', ') + '}' : size[0]});`);
  return [item.setup, ...allocations, '', item.body].join('\n');
}

// A complete standalone program for each typography task.
function textExample(id, title, description, body) {
  return {id, group: 'Use the library', title, description,
    covers: id === 'font_memory' ? ['FontCollection'] : ['FontCollection', 'Layout'], text: true,
    code: `#include <wgpupixel_text.h>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>

using namespace wgpupixel;
int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: example_${id} /path/to/font.ttf\\n";
        return 1;
    }
    try {
${body.split('\n').map(line => line ? '        ' + line : '').join('\n')}
    } catch (const Error& error) {
        std::cerr << error.what() << '\\n';
        return 1;
    }
}
`};
}

// Complete workflows live alongside operation examples, not in a second source tree.
examples.push(
  {id: 'memory_budget', group: 'Use the library', title: 'Budget a GPU cache', description: 'Count requested GPU bytes by category, evict a cached image before reserving its replacement, and enforce a hard allocation budget.', covers: ['Context.memory', 'Context.set_memory_limit', 'Context.reset_peak', 'Context.limits'], code: String.raw`#include <wgpupixel.h>
#include <iostream>

using namespace wgpupixel;

int main() {
    auto ctx = Context::create();
    constexpr std::uint64_t budget = 256ull * 1024 * 1024;
    ctx.set_memory_limit(budget);
    auto cached = ctx.create_image({3840, 2160});
    ctx.reset_peak();

    constexpr ImageSize next_size{4096, 2160};
    constexpr std::uint64_t next_bytes = 4096ull * 2160 * 16;
    const auto limits = ctx.limits();
    if (next_size.width > limits.max_image_dimension ||
        next_size.height > limits.max_image_dimension ||
        next_bytes / 16 > limits.max_image_pixels) return 1;
    const auto usage = ctx.memory();
    bool evicted = usage.total > budget || next_bytes > budget - usage.total;
    if (evicted) ctx.destroy(cached); // Wait for any submissions using it first.
    auto replacement = ctx.create_image(next_size);
    if (!evicted) ctx.destroy(cached);
    std::cout << "GPU bytes: " << ctx.memory().total
              << "; peak: " << ctx.memory().peak << '\n';
    ctx.destroy(replacement);
}
`},
  {id: "transfers", group: 'Use the library', title: "Transfer pixels", description: "Upload straight-alpha sRGB RGBA8 bytes, edit in linear float32, and download RGBA8. Submit and wait before reading; transfer buffers can be reused.", covers: ["upload:Image","download:Image"], code: String.raw`// Upload RGBA8 pixels, process in float32, and download RGBA8 pixels.

#include <wgpupixel.h>
#include <iostream>
#include <vector>

using namespace wgpupixel;

int main() {
    // sRGB RGBA8: an opaque white image at 1920 × 1080.
    std::vector<std::uint8_t> pixels(1920 * 1080 * 4, 255);
    std::vector<std::uint8_t> result(pixels.size());

    auto ctx = Context::create();
    auto image = ctx.create_image({1920, 1080});
    auto upload = ctx.create_upload_buffer(image);
    auto readback = ctx.create_readback_buffer(image);
    std::cout << "Transfer capacities: " << upload.capacity_pixels() << ", "
              << readback.capacity_pixels() << " pixels, "
              << upload.bytes_per_pixel() << " bytes per uploaded pixel, "
              << readback.bytes_per_pixel() << " bytes per downloaded pixel\n";

    ctx.write(upload, pixels); // Copy CPU bytes into reusable transfer storage.

    auto cmd = ctx.create_commands();
    cmd.upload(upload, image); // GPU conversion: sRGB -> linear, then premultiply alpha.
    cmd.brightness(image, {.amount = -0.25f});
    cmd.download(image, readback); // Unpremultiply, encode sRGB, and quantize to RGBA8.

    auto done = ctx.submit(cmd);
    ctx.wait(done);
    ctx.read(readback, result); // Completed download; result must have the exact byte count.

    std::cout << "First RGBA pixel: " << unsigned(result[0]) << ", " << unsigned(result[1]) << ", "
              << unsigned(result[2]) << ", " << unsigned(result[3]) << '\n';

    ctx.destroy(upload);
    ctx.destroy(readback);
    ctx.destroy(image);
}
`},
  {id: "transfer_formats", group: 'Use the library', title: "16-bit, float and partial transfers", description: "Each transfer buffer has a CPU format: rgba8 and rgba16 are straight-alpha sRGB; rgba32_float is linear premultiplied storage, lossless and HDR-safe. An optional region moves one rectangle as tightly packed rows, for tiles and eyedroppers.", covers: ["upload:Image","download:Image","TransferFormat"], code: String.raw`#include <wgpupixel.h>
#include <array>
#include <iostream>
#include <vector>

using namespace wgpupixel;

int main() {
    auto ctx = Context::create();
    auto canvas = ctx.create_image({1920, 1080});

    // 16-bit straight-alpha sRGB, as decoded from a 16-bit PNG or TIFF: 8 bytes per pixel.
    std::vector<std::uint16_t> photo(1920 * 1080 * 4, 65535);
    auto upload = ctx.create_upload_buffer(canvas, {.format = TransferFormat::rgba16});
    ctx.write(upload, {reinterpret_cast<const std::uint8_t*>(photo.data()), photo.size() * 2});

    // A tile update needs a buffer for the tile only; rows of the rectangle are packed.
    const Rect tile{512, 256, 256, 256};
    std::vector<std::uint8_t> tile_pixels(256 * 256 * 4, 128); // RGBA8 by default.
    auto tile_upload = ctx.create_upload_buffer(canvas, {.capacity_pixels = 256 * 256});
    ctx.write(tile_upload, tile_pixels);

    // Eyedropper: one linear premultiplied float pixel; values above 1 are preserved.
    auto probe = ctx.create_readback_buffer(
        canvas, {.format = TransferFormat::rgba32_float, .capacity_pixels = 1});

    ctx.run_and_wait([&](Commands& cmd) {
        cmd.upload(upload, canvas);
        cmd.upload(tile_upload, canvas, {.region = tile});
        cmd.exposure(canvas, {.stops = 1});
        cmd.download(canvas, probe, {.region = Rect{600, 300, 1, 1}});
    });
    std::array<float, 4> sample{};
    ctx.read(probe, {reinterpret_cast<std::uint8_t*>(sample.data()), sizeof(sample)});
    std::cout << "Linear premultiplied RGBA: " << sample[0] << ", " << sample[1] << ", "
              << sample[2] << ", " << sample[3] << '\n'; // About 0.217, 0.217, 0.217, 0.502.

    ctx.destroy(probe);
    ctx.destroy(tile_upload);
    ctx.destroy(upload);
    ctx.destroy(canvas);
}
`},
  {id: "masks", group: 'Use the library', title: "Coverage masks", description: "An A8 mask stores one linear coverage byte per pixel. Upload, fill, copy, invert, extract and download coverage; the same mask can limit most pixel operations.", covers: ["upload:Mask","download:Mask","fill:Mask","copy:Mask","invert:Mask","extract_mask"], code: String.raw`#include <wgpupixel.h>

#include <array>
#include <cstdint>
#include <iostream>

using namespace wgpupixel;

int main() {
    auto ctx = Context::create();
    auto image = ctx.create_image({3, 1});
    auto mask = ctx.create_mask({3, 1});
    auto inverse = ctx.create_mask({3, 1});
    auto upload = ctx.create_upload_buffer(mask);
    auto readback = ctx.create_readback_buffer(image);
    auto mask_readback = ctx.create_readback_buffer(mask);
    // Three linear coverage bytes: unchanged, about half, and full effect.
    const std::array<std::uint8_t, 3> coverage{0, 128, 255};
    ctx.write(upload, coverage);

    auto cmd = ctx.create_commands(5);
    cmd.upload(upload, mask);
    cmd.fill(image, {.color = {0, 0, 0.25f, 0.5f}}); // Linear premultiplied half-alpha blue.
    cmd.fill(image, {.color = {0.5f, 0, 0, 0.5f}, .mask = &mask});
    cmd.brightness(image, {.amount = 0.1f, .mask = &mask});
    cmd.download(image, readback);
    ctx.submit_and_wait(cmd);

    std::array<std::uint8_t, 12> output{};
    ctx.read(readback, output); // Exported image pixels are straight sRGB RGBA8.
    for (std::size_t i = 0; i < coverage.size(); ++i) {
        std::cout << "coverage " << unsigned(coverage[i]) << ":";
        for (std::size_t c = 0; c < 4; ++c) {
            std::cout << ' ' << unsigned(output[i * 4 + c]);
        }
        std::cout << '\n';
    }

    // Mask-only operations use the same command and transfer API, one byte per pixel.
    cmd.copy(mask, inverse);
    cmd.invert(inverse); // 0 -> 255; 128 -> 127; 255 -> 0.
    cmd.download(inverse, mask_readback);
    ctx.submit_and_wait(cmd);
    std::array<std::uint8_t, 3> inverted{};
    ctx.read(mask_readback, inverted);
    std::cout << "Inverted coverage:";
    for (auto value : inverted) std::cout << ' ' << unsigned(value);
    std::cout << '\n';

    cmd.fill(mask, {.coverage = 0.5f}); // Uniform coverage, quantized to bytes.
    cmd.extract_mask(image, inverse, {.mode = MaskMode::alpha}); // Or linear luminance.
    // Masked out-of-place operations preserve destination pixels where coverage is zero.
    auto result = ctx.create_image({3, 1});
    cmd.fill(result, {.color = {0, 0, 0, 0}}); // The destination must already contain valid pixels.
    cmd.copy(image, result, {.mask = &inverse});
    ctx.submit_and_wait(cmd);

    mask.set_size({1, 1}); // Logical dimensions only: no resize or clear, capacity stays 3.
    std::cout << mask.size().width << 'x' << mask.size().height << ", capacity "
              << mask.capacity_pixels() << ", bytes/pixel " << mask_readback.bytes_per_pixel() << '\n';

    ctx.destroy(result);
    ctx.destroy(mask_readback);
    ctx.destroy(readback);
    ctx.destroy(upload);
    ctx.destroy(inverse);
    ctx.destroy(mask);
    ctx.destroy(image);
}
`},
  {id: "files", group: 'Use the library', title: "Load and save images", description: "Native file I/O decodes into linear premultiplied pixels and converts tagged colors. Saving writes an 8-bit sRGB PNG. Run with an input filename and output filename.", covers: ["io.load","io.save"], code: String.raw`// Load a file, apply grayscale on the GPU, and save an sRGB PNG.

#include <wgpupixel_io.h>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: example-files input output.png\n";
        return 1;
    }
    try {
        using namespace wgpupixel;
        auto ctx = Context::create();
        // Decode, apply orientation/color conversion, and return a caller-owned Image.
        auto image = io::load(ctx, argv[1]);
        auto cmd = ctx.create_commands();
        cmd.grayscale(image);
        ctx.submit_and_wait(cmd); // Finish editing before the synchronous save.
        io::save(ctx, image, argv[2]); // RGBA8 sRGB PNG; overwrites the output path.
        ctx.destroy(image);
    } catch (const wgpupixel::Error& error) {
        std::cerr << error.operation() << ": " << error.what() << '\n';
        return 1;
    }
}
`},
  {id: "lifetime", group: 'Use the library', title: "Own resources explicitly", description: "Image copies alias one allocation. Storage is released when its last owner lets go; recordings and pending submissions retain what they use. After waiting, destroy can release storage explicitly and invalidate all aliases.", covers: ["Context.create","Context.prepare","Context.create_image","Context.create_commands","Context.submit","Context.wait","Context.destroy"], code: String.raw`// Create handles, move ownership, copy aliases, and release resources.

#include <wgpupixel.h>
#include <utility>

using namespace wgpupixel;

int main() {
    // Default handles are empty. Factories attach them to a context.
    Context context;
    context = Context::create(); // Device now; each pipeline compiles on its first submission.
    context.prepare(); // Optional: compile every pipeline up front. Idempotent.
    auto ctx = std::move(context); // Context is movable, not copyable.
    Image image;
    UploadBuffer upload;
    ReadbackBuffer readback;
    Commands recording;
    Submission pending;
    image = ctx.create_image({1920, 1080});
    upload = ctx.create_upload_buffer(image);
    readback = ctx.create_readback_buffer(image);
    recording = ctx.create_commands(1); // Capacity for one recorded operation.

    // Moving commands transfers the recording; copying an Image only aliases it.
    auto cmd = std::move(recording);
    recording = std::move(cmd);
    // UploadBuffer and ReadbackBuffer copies also alias their original storage.
    auto alias = image;
    recording.fill(alias, {.color = {0.2f, 0.5f, 0.8f, 1.0f}});
    pending = ctx.submit(recording);
    auto ticket = pending; // Submission copies refer to the same completion.
    ctx.wait(ticket);

    ctx.destroy(upload);
    ctx.destroy(readback);
    ctx.destroy(alias); // Invalidates image too; never destroy it twice.
    // Commands release their own bookkeeping when leaving scope.
}
`},
  {id: "reuse", group: 'Use the library', title: "Reuse an allocation", description: "set_size changes logical dimensions within existing capacity; it does not resample or clear pixels. Revisions track writes, not immutable snapshots.", covers: ["Image.set_size"], code: String.raw`// Reserve 4K storage once and reuse it for 1080p operations.

#include <wgpupixel.h>
#include <iostream>

using namespace wgpupixel;

int main() {
    auto ctx = Context::create();
    auto work = ctx.create_image({3840, 2160});
    auto cmd = ctx.create_commands(2); // One slot for fill, one for brightness.

    // set_size changes logical dimensions, preserving the allocation and its contents.
    work.set_size({1920, 1080});
    std::cout << work.size().width << " x " << work.size().height
              << ", capacity: " << work.capacity_pixels() << " pixels\n";

    const float adjustments[] = {0.0f, 0.1f, 0.2f};
    for (float amount : adjustments) {
        // Initialize the logical area before applying an in-place adjustment.
        cmd.fill(work, {.color = {0.1f, 0.4f, 0.7f, 1.0f}});
        cmd.brightness(work, {.amount = amount});

        auto done = ctx.submit(cmd);
        std::cout << "Submitted revision: " << work.revision() << '\n';
        ctx.wait(done); // Reuse the same command storage on the next iteration.
    }

    ctx.destroy(work);
}
`},
  {id: "errors", group: 'Use the library', title: "Handle an error", description: "Invalid arguments throw structured errors before recording that operation. Inspect code, operation, parameter and message; fix the call before continuing.", covers: ["Error"], code: String.raw`// Inspect a validation error, correct its argument, and continue recording.

#include <wgpupixel.h>
#include <iostream>

using namespace wgpupixel;

void describe(const Error& error) {
    std::cout << static_cast<unsigned>(error.code()) << ' '
              << error.operation() << '.' << error.parameter() << ": " << error.what() << '\n';
}

int main() {
    auto ctx = Context::create();
    auto image = ctx.create_image({1920, 1080});
    auto cmd = ctx.create_commands();
    cmd.fill(image, {.color = {0.2f, 0.5f, 0.8f, 1.0f}});

    try {
        cmd.gaussian_blur(image, {.radius = 18, .sigma = 6.0f}); // Required workspace was omitted.
    } catch (const Error& error) {
        if (error.code() != ErrorCode::capacity) throw;
        describe(error);
    }

    // Failed validation added no commands. Correct the argument and continue.
    auto workspace = ctx.create_workspace(gaussian_blur_requirements(image.size(), {.radius = 18, .sigma = 6.0f}).workspace);
    cmd.gaussian_blur(image, {.radius = 18, .sigma = 6.0f, .workspace = workspace});
    ctx.submit_and_wait(cmd);
    ctx.destroy(workspace);
    ctx.destroy(image);

    // An application adapter can also construct the same error type.
    describe(Error(ErrorCode::invalid_argument, "adapter", "opacity", "Expected a finite value"));
}
`},
  textExample('text', 'Draw text on an image', 'Load a font and composite glyph coverage on the GPU. The raster origin preserves the layout position. Run with a font filename; link wgpupixel::text.', String.raw`text::FontCollection fonts;
const auto families = fonts.add_file(argv[1]);
text::TextStyle style;
style.family = families.at(0);
style.size_px = 48;
auto layout = fonts.layout("Hello, pixels", style);
const auto raster = layout.rasterize(text::RasterFormat::a8);
if (raster.pixels.empty()) {
    return 0;
}

auto ctx = Context::create();
auto mask = ctx.create_mask({raster.width, raster.height});
auto layer = ctx.create_image({raster.width, raster.height});
auto canvas = ctx.create_image({640, 240});
auto upload = ctx.create_upload_buffer(mask);
ctx.write(upload, raster.pixels);
auto commands = ctx.create_commands(5);
commands.upload(upload, mask);
commands.fill(layer, {.color = {0, 0, 0, 0}});
// Color is linear premultiplied; glyph coverage multiplies all four channels.
commands.fill(layer, {.color = {0.4f, 0.15f, 0.05f, 0.5f}, .mask = &mask});
commands.fill(canvas, {.color = {0.08f, 0.08f, 0.08f, 1}});
// Raster origin preserves bearings and baseline relative to layout position.
commands.blend(layer, canvas, {.position = {24 + raster.origin_x, 24 + raster.origin_y}});
ctx.submit_and_wait(commands);
auto readback = ctx.create_readback_buffer(canvas);
auto download = ctx.create_commands(1);
download.download(canvas, readback);
ctx.submit_and_wait(download);
std::vector<std::uint8_t> pixels(std::size_t(canvas.size().width) * canvas.size().height * 4);
ctx.read(readback, pixels); // Straight-alpha sRGB RGBA8, ready for an encoder.
std::cout << families.at(0) << ": " << layout.metrics().line_count << " lines, "
          << pixels.size() << " composited RGBA bytes\n";
ctx.destroy(readback);
ctx.destroy(upload);
ctx.destroy(mask);
ctx.destroy(layer);
ctx.destroy(canvas);`),
  textExample('text_layout', 'Style and edit text', 'Wrapping, UTF-8 style ranges, hit testing, carets and selections. Ranges replace the complete style. Run with a font filename.', String.raw`text::FontCollection fonts;
const auto families = fonts.add_file(argv[1]);
text::TextStyle style;
style.family = families.at(0);
style.size_px = 48;
text::ParagraphStyle paragraph;
paragraph.width_px = 480;
paragraph.wrap = text::Wrap::word_char;
paragraph.alignment = text::Alignment::left;
paragraph.direction = text::Direction::auto_detect;
// Ranges replace the whole style and use UTF-8 byte offsets.
auto accent = style;
accent.weight = 700;
accent.color = {0.8f, 0.2f, 0.05f, 1};
const std::array ranges{text::StyleRange{0, 8, accent}};
auto layout = fonts.layout("External fonts\nEditable UTF-8 text", style, paragraph, ranges);
const auto metrics = layout.metrics();
const auto hit = layout.hit_test(30, 20);
const auto caret = layout.caret(hit.byte_index);
const auto selection = layout.selection(0, 8);
// hit.trailing counts Unicode characters within the grapheme, not bytes.
std::cout << "Hit byte " << hit.byte_index << ", leading caret x " << caret.strong.x
          << ", selection rectangles " << selection.size() << '\n';
// RGBA preserves style colors; use A8 when only glyph coverage is needed.
const auto colored = layout.rasterize(text::RasterFormat::rgba8);
std::cout << metrics.line_count << " lines, " << colored.width << " x " << colored.height << ", RGBA stride "
          << colored.stride << " bytes\n";`),
  textExample('font_memory', 'Load a font from memory', 'Fonts are copied into a private collection. List the registered families without modifying system fonts. Run with a font filename.', String.raw`std::ifstream file(argv[1], std::ios::binary);
const std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>{file}, {});
text::FontCollection memory_fonts;
const auto registered = memory_fonts.add_bytes(bytes);
std::cout << registered.at(0) << ": " << memory_fonts.families().size()
          << " memory font families\n";`),
  {id: "presentation_texture", group: 'Use the library', title: "Present into your texture", description: "Use the context’s borrowed device and queue. Keep target textures alive until the draw completes. The renderer receives a GPU texture, without a pixel download.", covers: ["webgpu.native_context","Presenter"], code: String.raw`// Draw a 1080p image into a texture for a UI renderer.

#include <wgpupixel_webgpu.h>
#include <stdexcept>
#include <utility>

using namespace wgpupixel;

struct Target {
    WGPUTexture texture = nullptr;
    WGPUTextureView view = nullptr;
    ~Target() {
        if (view) wgpuTextureViewRelease(view);
        if (texture) wgpuTextureRelease(texture);
    }
};

int main() {
    auto ctx = Context::create();
    auto gpu = webgpu::native_context(ctx); // Borrowed instance, adapter, device, queue.
    auto image = ctx.create_image({1920, 1080});
    auto cmd = ctx.create_commands();

    // A UI renderer on this device can sample the result as a texture.
    Target target;
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = {1920, 1080, 1};
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    target.texture = wgpuDeviceCreateTexture(gpu.device, &desc);
    if (!target.texture) throw std::runtime_error("Could not create display texture");
    target.view = wgpuTextureCreateView(target.texture, nullptr);

    // Create the presenter once for this target format and reuse it across frames.
    webgpu::Presenter presenter;
    presenter = webgpu::Presenter::create(ctx, desc.format);
    auto display = std::move(presenter); // Presenter is movable, not copyable.
    cmd.fill(image, {.color = {0.2f, 0.5f, 0.8f, 1.0f}});
    auto edited = ctx.submit(cmd);
    // The shared queue orders edits before drawing. Keep the target alive until wait.
    ctx.wait(display.draw(image, target.view, 1920, 1080));
    ctx.wait(edited);

    ctx.destroy(image);
    // target.view contains the displayed pixels for the application's renderer.
}
`},
  {id: 'display', group: 'Use the library', title: 'Give the UI a display texture', description: 'Display owns a presentation texture. Register its borrowed view in a UI renderer on the same device; unregister it before close. Wait before editing its input image again.', covers: ['Display', 'Context.is_complete'], code: String.raw`#include <wgpupixel_webgpu.h>
using namespace wgpupixel;
int main() {
    auto ctx = Context::create();
    auto image = ctx.create_image({320, 200});
    auto cmd = ctx.create_commands();
    cmd.fill(image, {.color = {0.05f, 0.2f, 0.5f, 1}});
    ctx.submit_and_wait(cmd);
    auto display = webgpu::Display::create(ctx, 320, 200);
    const auto presented = display.draw(image);
    if (!ctx.is_complete(presented)) ctx.wait(presented);
    auto view = display.view(); // Borrow this in your UI, using this same device.
    (void)view;
    // Unregister the view from your UI before releasing it.
    display.close();
    ctx.destroy(image);
}
`}
);
