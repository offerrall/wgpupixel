//! coverage kernel
//! include filter_common
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;
fn apply_operation(id: vec3<u32>) {
    let index = id.y * params.dimensions.z + id.x;
    var position = (vec2<f32>(id.xy) + 0.5) / params.values.x;
    if (params.values.z == 1.0) {
        position = vec2<f32>(id.xy) * vec2<f32>(params.dimensions.xy - 1u) /
                   max(vec2<f32>(params.dimensions.zw - 1u), vec2<f32>(1.0)) + 0.5;
    }
    let value = sample_linear(position);
    let coverage = select(operation_coverage(index), 1.0, params.values.y == 1.0);
    if (coverage == 1.0) { destination[index] = value; }
    else if (coverage > 0.0) { destination[index] = mix(destination[index], value, coverage); }
}
