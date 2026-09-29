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

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) {
        return;
    }
    let key = (id.x * 0x1f123bb5u) ^ (id.y * 0x5f356495u) ^ params.reserved.x;
    let r = noise_value(key);
    var rgb = vec3<f32>(r);
    if (params.reserved.y == 0u) {
        rgb.g = noise_value(key + 0x9e3779b9u);
        rgb.b = noise_value(key + 0x3c6ef372u);
    }
    pixels[id.y * params.dimensions.x + id.x] = vec4<f32>(rgb, 1.0);
}
