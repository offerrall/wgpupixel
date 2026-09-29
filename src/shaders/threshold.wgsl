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
    let luma = dot(pixel.rgb / pixel.a, vec3<f32>(0.2126, 0.7152, 0.0722));
    let result = select(0.0, pixel.a, luma >= params.values.x);
    pixels[index] = vec4<f32>(vec3<f32>(result), pixel.a);
}
