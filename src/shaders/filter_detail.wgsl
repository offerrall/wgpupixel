//! include filter_common
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;
@group(0) @binding(4) var<storage, read> blurred: array<vec4<f32>>;
fn encoded(color: vec3<f32>) -> vec3<f32> {
    let v = max(color, vec3<f32>(0.0));
    return select(1.055 * pow(v, vec3<f32>(1.0 / 2.4)) - vec3<f32>(0.055),
                  12.92 * color, color <= vec3<f32>(0.0031308));
}
fn apply_operation(id: vec3<u32>) {
    let index = id.y * params.dimensions.x + id.x;
    let center = source[index];
    if (center.a <= 0.0) { destination[index] = vec4<f32>(0.0); return; }
    if (params.values.x == 0.0 && params.values.z == 0.0) { destination[index] = center; return; }
    let difference = straight(center) - straight(blurred[index]);
    var color = straight(center) + select(vec3<f32>(0.0), difference * params.values.x,
                                           abs(encoded(straight(center)) - encoded(straight(blurred[index]))) >= vec3<f32>(params.values.y));
    if (params.values.z == 1.0) { color = vec3<f32>(0.5) + difference; }
    destination[index] = vec4<f32>(color * center.a, center.a);
}
