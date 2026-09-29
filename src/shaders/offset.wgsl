//! include footprint
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;

fn footprint_table(index: u32) -> vec4<f32> {
    return vec4<f32>(0.0);
}

fn footprint_fetch(pixel: vec2<i32>) -> vec4<f32> {
    return source[u32(pixel.y) * params.dimensions.x + u32(pixel.x)];
}

// Exact integer placement: source = destination - offset, with params.offsets.xy
// holding the negated offset already reduced to a small range and
// params.reserved.y the edge mode.
fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.z || id.y >= params.dimensions.w) { return; }
    let pixel = vec2<i32>(id.xy) + params.offsets.xy;
    destination[id.y * params.dimensions.z + id.x] =
        footprint_pixel(pixel, vec2<i32>(params.dimensions.xy), params.reserved.y);
}
