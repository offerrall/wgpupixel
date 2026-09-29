@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;
fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) { return; }
    let index = id.y * params.dimensions.x + id.x;
    let pixel = pixels[index];
    if (pixel.a == 0.0) { pixels[index] = vec4<f32>(0.0); return; }
    if (params.color1.a == 0.0) { return; }
    var position = vec2<f32>(0.0);
    if (params.dimensions.x > 1u) { position.x = 2.0 * f32(id.x) / f32(params.dimensions.x - 1u) - 1.0; }
    if (params.dimensions.y > 1u) { position.y = 2.0 * f32(id.y) / f32(params.dimensions.y - 1u) - 1.0; }
    let distance = length(position);
    var coverage = select(0.0, 1.0, distance >= params.values.x);
    if (params.values.y > 0.0) {
        // Difference then division avoids overflowing radius +/- softness/2.
        let t = clamp((distance - params.values.x) / params.values.y + 0.5, 0.0, 1.0);
        coverage = t * t * (3.0 - 2.0 * t);
    }
    let rgb = pixel.rgb * (1.0 - coverage * params.color1.a) + params.color1.rgb * (coverage * pixel.a);
    pixels[index] = vec4<f32>(rgb, pixel.a);
}
