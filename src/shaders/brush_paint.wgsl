//! include painting_blend brush_engine brush_round brush_paint_tools painting_color blend_modes blend_pixel blend_add blend_soft_light blend_hard_light
@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;
@group(0) @binding(7) var<storage, read> data: array<f32>;

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) { return; }
    let stroke = stroke_coverage(id.xy);
    if (stroke <= 0.0) { return; }
    let index = id.y * params.dimensions.x + id.x;
    pixels[index] = paint_tool(pixels[index], stroke);
}
