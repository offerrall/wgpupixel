// sRGB transfer, mirrored for negatives so HDR and negative values stay invertible.
fn srgb_encode(value: vec3<f32>) -> vec3<f32> {
    let v = abs(value);
    return sign(value) * select(1.055 * pow(v, vec3<f32>(1.0 / 2.4)) - 0.055, v * 12.92,
                                v <= vec3<f32>(0.0031308));
}

fn srgb_decode(value: vec3<f32>) -> vec3<f32> {
    let v = abs(value);
    return sign(value) *
           select(pow((v + 0.055) / 1.055, vec3<f32>(2.4)), v / 12.92, v <= vec3<f32>(0.04045));
}

// Uniform in [0, 1) with a 24-bit mantissa.
fn painting_random(input: u32) -> f32 {
    var value = input;
    value = (value ^ (value >> 16u)) * 0x7feb352du;
    value = (value ^ (value >> 15u)) * 0x846ca68bu;
    value = value ^ (value >> 16u);
    return f32(value >> 8u) * (1.0 / 16777216.0);
}
