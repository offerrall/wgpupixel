@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) {
        return;
    }
    let index = id.y * params.dimensions.x + id.x;
    let pixel = pixels[index];
    if (pixel.a == 0.0) {
        pixels[index] = vec4<f32>(0.0);
        return;
    }
    let above = pixel.rgb / pixel.a > vec3<f32>(params.values.x);
    let rgb = select(pixel.rgb, vec3<f32>(pixel.a) - pixel.rgb, above);
    pixels[index] = vec4<f32>(rgb, pixel.a);
}
