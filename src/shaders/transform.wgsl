//! include footprint
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;
// Box pyramid levels or the area table (see footprint_map); else the source itself.
@group(0) @binding(4) var<storage, read> pyramid: array<vec4<f32>>;

fn footprint_table(index: u32) -> vec4<f32> {
    return pyramid[index];
}

fn footprint_fetch(pixel: vec2<i32>) -> vec4<f32> {
    if (footprint_level > 0u) {
        return pyramid[footprint_offset + u32(pixel.y) * footprint_width + u32(pixel.x)];
    }
    return source[u32(pixel.y) * params.dimensions.x + u32(pixel.x)];
}

// Affine and projective transforms: each destination pixel center is mapped back
// into the source (see footprint_map) and filtered over its footprint.
fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.z || id.y >= params.dimensions.w) { return; }
    destination[id.y * params.dimensions.z + id.x] = footprint_map(vec2<f32>(id.xy) + vec2<f32>(0.5));
}
