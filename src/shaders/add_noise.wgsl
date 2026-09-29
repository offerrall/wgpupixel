@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;

fn noise_hash(input: u32) -> u32 {
    var value = input;
    value = (value ^ (value >> 16u)) * 0x7feb352du;
    value = (value ^ (value >> 15u)) * 0x846ca68bu;
    return value ^ (value >> 16u);
}

fn noise_value(key: u32) -> f32 {
    // Exactly representable 24-bit mantissa, always below one.
    return f32(noise_hash(key) >> 8u) * (1.0 / 16777216.0);
}

fn signed_noise(key: u32) -> f32 {
    let u = noise_value(key);
    if (params.reserved.z == 0u) { return 2.0 * u - 1.0; }
    let v = noise_value(key ^ 0xa511e9b3u);
    return sqrt(-2.0 * log(max(u, 1.0 / 16777216.0))) * cos(6.28318530718 * v);
}
fn apply_operation(id: vec3<u32>) {
    let index = id.y * params.dimensions.x + id.x;
    let original = pixels[index];
    if (params.values.x == 0.0) { return; }
    if (original.a <= 0.0) { pixels[index] = vec4<f32>(0.0); return; }
    let key = (id.x * 0x1f123bb5u) ^ (id.y * 0x5f356495u) ^ params.reserved.x;
    var noise = vec3<f32>(signed_noise(key));
    if (params.reserved.y == 0u) {
        noise.g = signed_noise(key + 0x9e3779b9u);
        noise.b = signed_noise(key + 0x3c6ef372u);
    }
    var color = original.rgb / original.a + noise * params.values.x;
    if (params.reserved.w != 0u) { color = clamp(color, vec3<f32>(0.0), vec3<f32>(1.0)); }
    pixels[index] = vec4<f32>(color * original.a, original.a);
}
