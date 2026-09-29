//! include footprint resize_nearest
//! coverage kernel
//! entry none
@group(0) @binding(0) var<storage, read> source: array<u32>;
@group(0) @binding(1) var<storage, read_write> destination: array<u32>;
// Box pyramid levels or the area table of the coverage (see footprint_map); a
// placeholder otherwise.
@group(0) @binding(4) var<storage, read> pyramid: array<vec4<f32>>;

fn footprint_table(index: u32) -> vec4<f32> {
    return pyramid[index];
}

fn footprint_fetch(pixel: vec2<i32>) -> vec4<f32> {
    if (footprint_level > 0u) {
        return pyramid[footprint_offset + u32(pixel.y) * footprint_width + u32(pixel.x)];
    }
    let index = u32(pixel.y) * params.dimensions.x + u32(pixel.x);
    if (params.reserved.z == 3u) {
        // Float intermediate of a large resize: coverage is its alpha.
        return vec4<f32>(bitcast<f32>(source[index * 4u + 3u]));
    }
    return vec4<f32>(f32((source[index / 4u] >> ((index % 4u) * 8u)) & 255u) / 255.0);
}

// params.reserved.z selects the mapping: 0 projective (footprint_map), 1 exact
// nearest resize, 2 exact integer placement source = sign * destination + shift
// with sign and shift bitcast into params.extra, 3 projective from a float source.
fn mask_coverage(x: u32, y: u32) -> f32 {
    if (params.reserved.z == 4u) {
        let position = params.values.yz + (vec2<f32>(f32(x), f32(y)) + vec2<f32>(0.5) -
                       vec2<f32>(params.dimensions.zw) * 0.5) * params.values.x;
        return footprint_sample(position, mat2x2<f32>(1.0, 0.0, 0.0, 1.0),
                                vec2<i32>(params.dimensions.xy), params.reserved.x, 0u, 1.0).x;
    }
    if (params.reserved.z == 1u) {
        let sx = resize_nearest_coordinate(x, params.dimensions.x, params.dimensions.z);
        let sy = resize_nearest_coordinate(y, params.dimensions.y, params.dimensions.w);
        return footprint_fetch(vec2<i32>(i32(sx), i32(sy))).x;
    }
    if (params.reserved.z == 2u) {
        let map = bitcast<vec4<i32>>(params.extra);
        let pixel = map.xy * vec2<i32>(i32(x), i32(y)) + map.zw;
        return footprint_pixel(pixel, vec2<i32>(params.dimensions.xy), params.reserved.y).x;
    }
    return footprint_map(vec2<f32>(f32(x), f32(y)) + vec2<f32>(0.5)).x;
}

// One invocation per packed destination word. params.offsets holds the clipped
// region as left, top, right, bottom; the selection mask blends like image kernels.
@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    if (any(id.xy >= params.dispatch.zw)) { return; }
    let width = params.dimensions.z;
    let count = width * params.dimensions.w;
    let word = id.y * params.dispatch.z + id.x;
    if (word >= count / 4u + min(count % 4u, 1u)) { return; }
    let region = vec4<u32>(params.offsets);
    let before = destination[word];
    var packed = 0u;
    for (var lane = 0u; lane < 4u; lane++) {
        let index = word * 4u + lane;
        var byte = (before >> (lane * 8u)) & 255u;
        let x = index % width;
        let y = index / width;
        if (index < count && x >= region.x && y >= region.y && x < region.z && y < region.w) {
            let selection = operation_coverage(index);
            if (selection > 0.0) {
                let value = clamp(mask_coverage(x, y), 0.0, 1.0);
                byte = u32(floor(mix(f32(byte) / 255.0, value, selection) * 255.0 + 0.5));
            }
        }
        packed |= byte << (lane * 8u);
    }
    destination[word] = packed;
}
