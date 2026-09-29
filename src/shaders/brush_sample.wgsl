//! include painting_blend brush_engine brush_round brush_sample_tools painting_sample blend_modes blend_pixel blend_add blend_soft_light blend_hard_light
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;
@group(0) @binding(7) var<storage, read> data: array<f32>;

fn sample_size() -> vec2<u32> { return params.dimensions.xy; }
fn sample_texel(index: u32) -> vec4<f32> { return source[index]; }

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.z || id.y >= params.dimensions.w) { return; }
    let stroke = stroke_coverage(id.xy);
    if (stroke <= 0.0) { return; }
    let index = id.y * params.dimensions.z + id.x;
    destination[index] = sample_tool(destination[index], stroke, id.xy);
}
