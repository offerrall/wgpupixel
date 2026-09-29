//! include footprint
//! coverage none
//! entry none
@group(0) @binding(0) var<storage, read> source: array<u32>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;

fn footprint_table(index: u32) -> vec4<f32> {
    return destination[index];
}

// Source pixels are float RGBA, or packed coverage when params.reserved.y is 1.
fn footprint_fetch(pixel: vec2<i32>) -> vec4<f32> {
    let index = u32(pixel.y) * params.dimensions.x + u32(pixel.x);
    if (params.reserved.y == 1u) {
        return vec4<f32>(f32((source[index / 4u] >> ((index % 4u) * 8u)) & 255u) / 255.0);
    }
    let base = index * 4u;
    return bitcast<vec4<f32>>(vec4<u32>(source[base], source[base + 1u], source[base + 2u],
                                        source[base + 3u]));
}

// Builds the area table read by area_integral (footprint.wgsl) for the source of size
// params.dimensions.xy. Pass params.reserved.x == 0: one invocation per 64-pixel chunk
// and row writes inclusive prefix sums within the chunk. Pass 1: one invocation per
// 4096-pixel group and row writes the group total after the row's prefix sums.
@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let w = params.dimensions.x;
    if (id.y >= params.dimensions.y) { return; }
    let row = id.y * area_stride(w);
    if (params.reserved.x == 0u) {
        let start = id.x * area_chunk;
        if (start >= w) { return; }
        var sum = vec4<f32>(0.0);
        for (var i = start; i < min(start + area_chunk, w); i++) {
            sum += footprint_fetch(vec2<i32>(i32(i), i32(id.y)));
            destination[row + i] = sum;
        }
        return;
    }
    let chunks = (w + area_chunk - 1u) / area_chunk;
    let first = id.x * 64u;
    if (first >= chunks) { return; }
    var sum = vec4<f32>(0.0);
    for (var c = first; c < min(first + 64u, chunks); c++) {
        sum += destination[row + min(c * area_chunk + area_chunk - 1u, w - 1u)];
    }
    destination[row + w + id.x] = sum;
}
