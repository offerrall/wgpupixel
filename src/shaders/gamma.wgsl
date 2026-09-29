@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;

fn signed_power(value: f32, exponent: f32) -> f32 {
    if (value == 0.0) {
        return 0.0;
    }
    return sign(value) * pow(abs(value), exponent);
}

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
    let straight = pixel.rgb / pixel.a;
    let exponent = params.values.x;
    let rgb = vec3<f32>(signed_power(straight.r, exponent),
                        signed_power(straight.g, exponent),
                        signed_power(straight.b, exponent));
    pixels[index] = vec4<f32>(rgb * pixel.a, pixel.a);
}
