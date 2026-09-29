@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) {
        return;
    }
    let index = id.y * params.dimensions.x + id.x;
    if (params.values.y == 0.0) {
        pixels[index] = params.color2;
        return;
    }
    let position = vec2<f32>(id.xy) - params.extra.xy;
    let delta = position - round(position / params.values.x) * params.values.x;
    // Scaling avoids overflow in length() for large finite spacing.
    let scale = max(1.0, max(abs(delta.x), abs(delta.y)));
    let distance = length(delta / scale) * scale;
    let t = clamp(0.5 + (params.values.y - distance) / params.values.z, 0.0, 1.0);
    let coverage = t * t * (3.0 - 2.0 * t);
    pixels[index] = mix(params.color2, params.color1, coverage);
}
