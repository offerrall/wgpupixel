//! include sampling sample_bicubic sample_lanczos
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;
const transparent_border = true;

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.z || id.y >= params.dimensions.w) {
        return;
    }
    let relative = vec2<f32>(id.xy) + vec2<f32>(0.5) - vec2<f32>(params.dimensions.zw) * 0.5;
    let position = params.values.yz + relative * params.values.x;
    let index = id.y * params.dimensions.z + id.x;
    destination[index] = sample_transform(position - vec2<f32>(0.5), params.reserved.x);
}
