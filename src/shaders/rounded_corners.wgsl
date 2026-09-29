@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;
fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) { return; }
    let radius = params.values.x;
    let half_size = vec2<f32>(params.dimensions.xy) * 0.5;
    let point = abs(vec2<f32>(id.xy) + vec2<f32>(0.5) - half_size);
    let q = point - (half_size - vec2<f32>(radius));
    if (q.x <= 0.0 || q.y <= 0.0) { return; }
    let distance = length(q) - radius;
    let t = clamp(-distance, 0.0, 1.0);
    let coverage = t * t * (3.0 - 2.0 * t);
    pixels[id.y * params.dimensions.x + id.x] *= coverage;
}
