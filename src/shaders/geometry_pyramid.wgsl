//! include footprint
//! coverage none
//! entry none
@group(0) @binding(0) var<storage, read> source: array<u32>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;

fn footprint_table(index: u32) -> vec4<f32> {
    return vec4<f32>(0.0);
}

fn footprint_fetch(pixel: vec2<i32>) -> vec4<f32> {
    return vec4<f32>(0.0);
}

fn pyramid_source(pixel: vec2<u32>) -> vec4<f32> {
    let index = pixel.y * params.dimensions.x + pixel.x;
    if (params.reserved.y == 1u) {
        return vec4<f32>(f32((source[index / 4u] >> ((index % 4u) * 8u)) & 255u) / 255.0);
    }
    let base = index * 4u;
    return bitcast<vec4<f32>>(vec4<u32>(source[base], source[base + 1u], source[base + 2u],
                                        source[base + 3u]));
}

// Builds pyramid level params.reserved.x (>= 1) into destination, which holds levels
// 1, 2, ... consecutively. Each pixel averages its 2x2 children on the previous level
// (the source for level 1; params.reserved.y is 1 for packed coverage). Children
// beyond an odd edge follow the edge mode params.reserved.z, so transparent pads with
// zero coverage and the level still spans exactly twice the previous pixel size.
@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    var size = params.dimensions.xy;
    var offset = 0u;
    var previous = size;
    var previous_offset = 0u;
    for (var k = 1u; k <= params.reserved.x; k++) {
        previous = size;
        previous_offset = offset;
        if (k > 1u) { offset += size.x * size.y; }
        size = (size + vec2<u32>(1u)) / 2u;
    }
    if (any(id.xy >= size)) { return; }
    var sum = vec4<f32>(0.0);
    for (var j = 0u; j < 2u; j++) {
        for (var i = 0u; i < 2u; i++) {
            let child = vec2<i32>(2u * id.xy + vec2<u32>(i, j));
            let x = footprint_axis(child.x, i32(previous.x), params.reserved.z);
            let y = footprint_axis(child.y, i32(previous.y), params.reserved.z);
            if (x < 0 || y < 0) { continue; }
            let pixel = vec2<u32>(u32(x), u32(y));
            if (params.reserved.x == 1u) {
                sum += pyramid_source(pixel);
            } else {
                sum += destination[previous_offset + pixel.y * previous.x + pixel.x];
            }
        }
    }
    destination[offset + id.y * size.x + id.x] = 0.25 * sum;
}
