//! include filter_common
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;
fn apply_operation(id: vec3<u32>) {
    let index = id.y * params.dimensions.x + id.x;
    let count = params.reserved.x;
    if (params.values.z == 0.0) { destination[index] = source[index]; return; }
    var result = vec4<f32>(0.0);
    for (var i = 0u; i < count; i += 1u) {
        let distance = (f32(i) / f32(count - 1u) - 0.5) * params.values.z;
        result += sample_linear(vec2<f32>(id.xy) + vec2<f32>(0.5) + params.values.xy * distance) / f32(count);
    }
    destination[index] = result;
}
