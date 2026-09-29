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
    if (params.values.x == 1.0) {
        return;
    }
    let pivot = vec3<f32>(0.5 * pixel.a);
    pixels[index] = vec4<f32>((pixel.rgb - pivot) * params.values.x + pivot, pixel.a);
}
