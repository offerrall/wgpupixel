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
    if (params.values.x == 0.0) {
        return;
    }
    let sepia = vec3<f32>(dot(pixel.rgb, vec3<f32>(0.393, 0.769, 0.189)),
                          dot(pixel.rgb, vec3<f32>(0.349, 0.686, 0.168)),
                          dot(pixel.rgb, vec3<f32>(0.272, 0.534, 0.131)));
    pixels[index] = vec4<f32>(mix(pixel.rgb, sepia, params.values.x), pixel.a);
}
