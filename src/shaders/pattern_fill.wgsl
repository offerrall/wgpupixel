//! include painting_blend painting_sample blend_modes blend_pixel blend_add blend_soft_light blend_hard_light
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;

fn sample_size() -> vec2<u32> { return params.dimensions.xy; }
fn sample_texel(index: u32) -> vec4<f32> { return source[index]; }

// params.extra, params.extra2.xy: inverse transform; values.x opacity; reserved.x mode.
fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.z || id.y >= params.dimensions.w) { return; }
    let p = vec2<f32>(id.xy) + vec2<f32>(0.5);
    let m = params.extra;
    let q = vec2<f32>(m.x * p.x + m.z * p.y, m.y * p.x + m.w * p.y) + params.extra2.xy;
    let index = id.y * params.dimensions.z + id.x;
    destination[index] = painting_blend(sampled_bilinear(q, true), destination[index],
                                     params.values.x, params.reserved.x);
}
