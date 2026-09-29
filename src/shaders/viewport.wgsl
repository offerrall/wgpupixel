// Editor viewport: pan/zoom/rotate view of `pixels` over a transparency checkerboard, with
// an optional selection overlay and pixel grid. Magnification samples nearest pixels.
// Minification box-filters each target pixel's footprint (its axis-aligned bounding box in
// image space) over one sampled level: an isotropic pyramid level, or for strongly
// anisotropic views a level reduced further along one axis. Every level texel holds the
// mean of the image pixels it covers, and each texel counts by its area inside the box.
struct View {
    inverse: vec4<f32>,     // Target -> image linear part: a, b, c, d.
    translation: vec4<f32>, // e, f, unused, zoom (target pixels per image pixel).
    info: vec4<u32>,        // Image width, height, unused, flags.
    checker: vec4<f32>,     // Canvas origin on the target x, y; cell size; grid threshold.
    grid: vec4<f32>,        // Image units per target pixel across x and y grid lines.
    box: vec4<f32>,         // Footprint half extents x, y; minified (0/1).
    background: vec4<f32>,
    checker_first: vec4<f32>,
    checker_second: vec4<f32>,
    overlay: vec4<f32>,
    grid_color: vec4<f32>,
    sample: vec4<u32>,       // Sampled level: image word offset, width, height, mask offset.
    sample_scale: vec4<f32>, // Image pixels per level texel along x, y; 1 samples pixels.
};
const view_srgb_target = 1u;
const view_overlay = 2u;
const view_overlay_selected = 4u;
const view_pixel_grid = 8u;

@group(0) @binding(2) var<uniform> view: View;
// Float32 levels: four words per image texel, one per mask texel. Mask pixels are bytes.
@group(0) @binding(3) var<storage, read> image_levels: array<f32>;
@group(0) @binding(4) var<storage, read> overlay_mask: array<u32>;
@group(0) @binding(5) var<storage, read> mask_levels: array<f32>;

fn image_pixel(texel: vec2<u32>) -> vec4<f32> {
    return pixels[texel.y * view.info.x + texel.x];
}
fn mask_pixel(texel: vec2<u32>) -> f32 {
    let index = texel.y * view.info.x + texel.x;
    return f32((overlay_mask[index / 4u] >> ((index % 4u) * 8u)) & 255u) / 255.0;
}

fn image_texel(texel: vec2<u32>) -> vec4<f32> {
    if (view.sample_scale.z != 0.0) {
        return image_pixel(texel);
    }
    let word = view.sample.x + 4u * (texel.y * view.sample.y + texel.x);
    return vec4<f32>(image_levels[word], image_levels[word + 1u], image_levels[word + 2u],
                     image_levels[word + 3u]);
}

fn mask_texel(texel: vec2<u32>) -> f32 {
    if (view.sample_scale.z != 0.0) {
        return mask_pixel(texel);
    }
    return mask_levels[view.sample.w + texel.y * view.sample.y + texel.x];
}

// Footprint box around p, clipped to the image, and the sampled texels it touches.
struct Footprint {
    low: vec2<f32>,
    high: vec2<f32>,
    first: vec2<u32>,
    last: vec2<u32>,
    area: f32,
};
fn footprint(p: vec2<f32>) -> Footprint {
    let size = vec2<f32>(view.info.xy);
    let low = clamp(p - view.box.xy, vec2<f32>(0.0), size);
    let high = clamp(p + view.box.xy, vec2<f32>(0.0), size);
    let scale = view.sample_scale.xy;
    let last_texel = view.sample.yz - vec2<u32>(1u);
    let first = min(vec2<u32>(low / scale), last_texel);
    let last = max(min(vec2<u32>(ceil(high / scale)), last_texel + vec2<u32>(1u)),
                   first + vec2<u32>(1u)) - vec2<u32>(1u);
    let extent = high - low;
    return Footprint(low, high, first, last, extent.x * extent.y);
}
// Share of the footprint covered by a texel. Normalizing before weighting keeps sums of
// HDR values near the float limit finite.
fn share(f: Footprint, texel: vec2<u32>) -> f32 {
    let size = vec2<f32>(view.info.xy);
    let scale = view.sample_scale.xy;
    let start = max(vec2<f32>(texel) * scale, f.low);
    let end = min(min(vec2<f32>(texel + vec2<u32>(1u)) * scale, size), f.high);
    let extent = max(end - start, vec2<f32>(0.0));
    return extent.x * extent.y / f.area;
}

fn sample_view(p: vec2<f32>) -> vec4<f32> {
    if (view.box.z == 0.0) {
        return image_pixel(vec2<u32>(p));
    }
    let f = footprint(p);
    if (f.area <= 0.0) {
        return image_texel(f.first);
    }
    var sum = vec4<f32>(0.0);
    var weights = 0.0;
    for (var y = f.first.y; y <= f.last.y; y += 1u) {
        for (var x = f.first.x; x <= f.last.x; x += 1u) {
            let weight = share(f, vec2<u32>(x, y));
            if (weight > 0.0) {
                sum += image_texel(vec2<u32>(x, y)) * weight;
                weights += weight;
            }
        }
    }
    return sum / weights;
}

fn sample_overlay(p: vec2<f32>) -> f32 {
    if (view.box.z == 0.0) {
        return mask_pixel(vec2<u32>(p));
    }
    let f = footprint(p);
    if (f.area <= 0.0) {
        return mask_texel(f.first);
    }
    var sum = 0.0;
    var weights = 0.0;
    for (var y = f.first.y; y <= f.last.y; y += 1u) {
        for (var x = f.first.x; x <= f.last.x; x += 1u) {
            let weight = share(f, vec2<u32>(x, y));
            sum += mask_texel(vec2<u32>(x, y)) * weight;
            weights += weight;
        }
    }
    return sum / max(weights, 1e-30);
}

// A grid line covers the target pixel whose center lies within half a pixel before it.
fn grid_line(coordinate: f32, extent: u32, units: f32) -> bool {
    let line = round(coordinate);
    let distance = (coordinate - line) / units;
    return line > 0.0 && line < f32(extent) && distance >= -0.5 && distance < 0.5;
}

fn over(top: vec4<f32>, bottom: vec4<f32>) -> vec4<f32> {
    return top + bottom * (1.0 - top.a);
}

fn display_color(rgba: vec4<f32>, srgb_target: bool) -> vec4<f32> {
    let alpha = clamp(rgba.a, 0.0, 1.0);
    if (alpha <= 0.0) {
        return vec4<f32>(0.0);
    }
    let encoded = encode(clamp(rgba.rgb / alpha, vec3<f32>(0.0), vec3<f32>(1.0))) * alpha;
    return vec4<f32>(select(encoded, decode(encoded), srgb_target), alpha);
}

@fragment fn viewport_fragment(in: Vertex) -> @location(0) vec4<f32> {
    let q = in.position.xy;
    let p = view.inverse.xy * q.x + view.inverse.zw * q.y + view.translation.xy;
    let size = vec2<f32>(view.info.xy);
    let flags = view.info.w;
    let srgb_target = (flags & view_srgb_target) != 0u;
    if (any(p < vec2<f32>(0.0)) || any(p >= size)) {
        return display_color(view.background, srgb_target);
    }
    var color = view.checker_first;
    if (view.checker.z > 0.0) {
        let cell = vec2<i32>(floor((q - view.checker.xy) / view.checker.z));
        if (((cell.x + cell.y) & 1) != 0) {
            color = view.checker_second;
        }
    }
    color = over(sample_view(p), color);
    if ((flags & view_overlay) != 0u) {
        let coverage = clamp(sample_overlay(p), 0.0, 1.0);
        let amount = select(1.0 - coverage, coverage, (flags & view_overlay_selected) != 0u);
        color = over(view.overlay * amount, color);
    }
    if ((flags & view_pixel_grid) != 0u && view.translation.w > view.checker.w &&
        (grid_line(p.x, view.info.x, view.grid.x) || grid_line(p.y, view.info.y, view.grid.y))) {
        color = over(view.grid_color, color);
    }
    return display_color(color, srgb_target);
}

// Pyramid reduction: target texel (x, y) is the mean of source texels 2x..2x+1, 2y..2y+1,
// each weighted by the image area it covers, so partial edge texels keep their true
// weight. Values stay float32: HDR averages are never clipped. Modes: 0 image -> level,
// 1 level -> level, 2 mask bytes -> level, 3 level -> level.
struct Reduce {
    source: vec4<u32>,      // width, height, word offset, mode
    destination: vec4<u32>, // width, height, word offset
    image: vec4<u32>,       // image width, height, source texel size in pixels, axis factor
};
@group(0) @binding(6) var<storage, read> reduce_source: array<u32>;
@group(0) @binding(7) var<storage, read_write> reduce_levels: array<f32>;
@group(0) @binding(8) var<uniform> reduce: Reduce;

fn reduce_fetch(texel: vec2<u32>) -> vec4<f32> {
    let index = texel.y * reduce.source.x + texel.x;
    switch reduce.source.w {
        case 0u: {
            let word = 4u * index;
            return bitcast<vec4<f32>>(vec4<u32>(reduce_source[word], reduce_source[word + 1u],
                                                reduce_source[word + 2u], reduce_source[word + 3u]));
        }
        case 1u: {
            let word = reduce.source.z + 4u * index;
            return vec4<f32>(reduce_levels[word], reduce_levels[word + 1u],
                             reduce_levels[word + 2u], reduce_levels[word + 3u]);
        }
        case 2u: {
            return vec4<f32>(f32((reduce_source[index / 4u] >> ((index % 4u) * 8u)) & 255u) / 255.0);
        }
        default: {
            return vec4<f32>(reduce_levels[reduce.source.z + index]);
        }
    }
}

// Image pixels covered by source texel t along one axis.
fn texel_extent(t: u32, extent: u32) -> f32 {
    let start = t * reduce.image.z;
    return f32(min(start + reduce.image.z, extent) - start);
}

@compute @workgroup_size(8, 8, 1)
fn reduce_level(@builtin(global_invocation_id) id: vec3<u32>) {
    if (any(id.xy >= reduce.destination.xy)) {
        return;
    }
    // Normalize weights first: sums of weighted HDR values could overflow float32.
    var total = 0.0;
    for (var dy = 0u; dy < 2u; dy += 1u) {
        for (var dx = 0u; dx < 2u; dx += 1u) {
            let texel = 2u * id.xy + vec2<u32>(dx, dy);
            if (all(texel < reduce.source.xy)) {
                total += texel_extent(texel.x, reduce.image.x) * texel_extent(texel.y, reduce.image.y);
            }
        }
    }
    var average = vec4<f32>(0.0);
    for (var dy = 0u; dy < 2u; dy += 1u) {
        for (var dx = 0u; dx < 2u; dx += 1u) {
            let texel = 2u * id.xy + vec2<u32>(dx, dy);
            if (all(texel < reduce.source.xy)) {
                let area = texel_extent(texel.x, reduce.image.x) * texel_extent(texel.y, reduce.image.y);
                average += reduce_fetch(texel) * (area / total);
            }
        }
    }
    let index = id.y * reduce.destination.x + id.x;
    if (reduce.source.w < 2u) {
        let word = reduce.destination.z + 4u * index;
        reduce_levels[word] = average.x;
        reduce_levels[word + 1u] = average.y;
        reduce_levels[word + 2u] = average.z;
        reduce_levels[word + 3u] = average.w;
    } else {
        reduce_levels[reduce.destination.z + index] = average.x;
    }
}

// Axis reduction for anisotropic views: target texel i averages `factor` consecutive source
// texels along axis destination.w (0 x, 1 y), weighted by the image pixels each covers.
// The source (image, mask bytes, or a pyramid level at source.z) is read-only here.
fn axis_fetch(texel: vec2<u32>) -> vec4<f32> {
    let index = texel.y * reduce.source.x + texel.x;
    switch reduce.source.w {
        case 0u, 1u: {
            let word = select(0u, reduce.source.z, reduce.source.w == 1u) + 4u * index;
            return bitcast<vec4<f32>>(vec4<u32>(reduce_source[word], reduce_source[word + 1u],
                                                reduce_source[word + 2u], reduce_source[word + 3u]));
        }
        case 2u: {
            return vec4<f32>(f32((reduce_source[index / 4u] >> ((index % 4u) * 8u)) & 255u) / 255.0);
        }
        default: {
            return vec4<f32>(bitcast<f32>(reduce_source[reduce.source.z + index]));
        }
    }
}

@compute @workgroup_size(8, 8, 1)
fn reduce_axis(@builtin(global_invocation_id) id: vec3<u32>) {
    if (any(id.xy >= reduce.destination.xy)) {
        return;
    }
    let along_y = reduce.destination.w == 1u;
    let factor = reduce.image.w;
    let position = select(id.x, id.y, along_y);
    let count = select(reduce.source.x, reduce.source.y, along_y);
    let extent = select(reduce.image.x, reduce.image.y, along_y);
    let first = position * factor;
    let last = min(first + factor, count);
    // Image pixels covered by the whole run; each texel weighs its own share.
    let covered = f32(min(last * reduce.image.z, extent) - first * reduce.image.z);
    var average = vec4<f32>(0.0);
    for (var t = first; t < last; t += 1u) {
        let texel = select(vec2<u32>(t, id.y), vec2<u32>(id.x, t), along_y);
        average += axis_fetch(texel) * (texel_extent(t, extent) / covered);
    }
    let index = id.y * reduce.destination.x + id.x;
    if (reduce.source.w < 2u) {
        let word = reduce.destination.z + 4u * index;
        reduce_levels[word] = average.x;
        reduce_levels[word + 1u] = average.y;
        reduce_levels[word + 2u] = average.z;
        reduce_levels[word + 3u] = average.w;
    } else {
        reduce_levels[reduce.destination.z + index] = average.x;
    }
}
