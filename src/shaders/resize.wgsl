//! include resize_nearest footprint
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;

fn footprint_table(index: u32) -> vec4<f32> {
    return vec4<f32>(0.0);
}

fn footprint_fetch(pixel: vec2<i32>) -> vec4<f32> {
    return source[u32(pixel.y) * params.dimensions.x + u32(pixel.x)];
}

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.z || id.y >= params.dimensions.w) { return; }
    if (params.reserved.x == 0u) {
        let x = resize_nearest_coordinate(id.x, params.dimensions.x, params.dimensions.z);
        let y = resize_nearest_coordinate(id.y, params.dimensions.y, params.dimensions.w);
        destination[id.y * params.dimensions.z + id.x] = source[y * params.dimensions.x + x];
        return;
    }
    // Edge-clamped, with kernels widened by the full reduction factor when downscaling.
    // Large reductions arrive split into one pass per axis (resize_pass).
    let scale = vec2<f32>(params.dimensions.xy) / vec2<f32>(params.dimensions.zw);
    let center = (vec2<f32>(id.xy) + vec2<f32>(0.5)) * scale;
    destination[id.y * params.dimensions.z + id.x] =
        footprint_sample(center, mat2x2<f32>(scale.x, 0.0, 0.0, scale.y),
                         vec2<i32>(params.dimensions.xy), params.reserved.x, 1u, 1e30);
}
