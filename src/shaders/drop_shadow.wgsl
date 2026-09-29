//! coverage none
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;
fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.z || id.y >= params.dimensions.w) { return; }
    let radius = u32(params.offsets.z);
    var pixel = vec4<f32>(0.0);
    if (id.x >= radius && id.y >= radius &&
        id.x - radius < params.dimensions.x && id.y - radius < params.dimensions.y) {
        let alpha = source[(id.y - radius) * params.dimensions.x + id.x - radius].a;
        pixel = params.color1 * alpha;
    }
    destination[id.y * params.dimensions.z + id.x] = pixel;
}
