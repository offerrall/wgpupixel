//! include sampling sample_bicubic sample_lanczos
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;
const transparent_border = true;

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.z || id.y >= params.dimensions.w) {
        return;
    }
    let source_size = vec2<f32>(params.dimensions.xy);
    let source_center = (source_size - vec2<f32>(1.0)) * 0.5;
    let destination_center = (vec2<f32>(params.dimensions.zw) - vec2<f32>(1.0)) * 0.5;
    let relative = vec2<f32>(id.xy) - destination_center;
    // Inverse of clockwise rotation in image coordinates (positive y downward).
    let position = source_center + vec2<f32>(
        params.values.x * relative.x + params.values.y * relative.y,
        -params.values.y * relative.x + params.values.x * relative.y);
    let index = id.y * params.dimensions.z + id.x;
    destination[index] = sample_transform(position, params.reserved.x);
}
