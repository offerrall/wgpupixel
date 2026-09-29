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
    let luma = vec3<f32>(dot(pixel.rgb, vec3<f32>(0.2126, 0.7152, 0.0722)));
    if (params.values.x <= 1.0) {
        // A convex combination stays representable even when rgb-luma does not.
        pixels[index] = vec4<f32>(luma * (1.0 - params.values.x) +
                                  pixel.rgb * params.values.x, pixel.a);
        return;
    }
    pixels[index] = vec4<f32>(luma + (pixel.rgb - luma) * params.values.x, pixel.a);
}
