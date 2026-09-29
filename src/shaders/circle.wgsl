@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) {
        return;
    }
    let size = vec2<f32>(params.dimensions.xy);
    let delta = vec2<f32>(id.xy) + vec2<f32>(0.5) - size * 0.5;
    let radius = min(size.x, size.y) * 0.5;
    let t = clamp(0.5 + (radius - length(delta)) / params.values.x, 0.0, 1.0);
    let coverage = t * t * (3.0 - 2.0 * t);
    pixels[id.y * params.dimensions.x + id.x] = mix(params.color2, params.color1, coverage);
}
