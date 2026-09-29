//! include footprint
//! coverage none
@group(0) @binding(0) var<storage, read> source: array<u32>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;

// Source pixels are float RGBA, or packed coverage when params.reserved.z is 1.
fn footprint_table(index: u32) -> vec4<f32> {
    return vec4<f32>(0.0);
}

fn footprint_fetch(pixel: vec2<i32>) -> vec4<f32> {
    let index = u32(pixel.y) * params.dimensions.x + u32(pixel.x);
    if (params.reserved.z == 1u) {
        return vec4<f32>(f32((source[index / 4u] >> ((index % 4u) * 8u)) & 255u) / 255.0);
    }
    let base = index * 4u;
    return bitcast<vec4<f32>>(vec4<u32>(source[base], source[base + 1u], source[base + 2u],
                                        source[base + 3u]));
}

// First pass of a large resize: an intermediate image reduced along one axis
// (the other axis keeps its size, so its kernel reads single pixels). The
// operation's mask and region apply only to the final pass.
fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.z || id.y >= params.dimensions.w) { return; }
    let scale = vec2<f32>(params.dimensions.xy) / vec2<f32>(params.dimensions.zw);
    let center = (vec2<f32>(id.xy) + vec2<f32>(0.5)) * scale;
    destination[id.y * params.dimensions.z + id.x] =
        footprint_sample(center, mat2x2<f32>(scale.x, 0.0, 0.0, scale.y),
                         vec2<i32>(params.dimensions.xy), params.reserved.x, 1u, 1e30);
}
