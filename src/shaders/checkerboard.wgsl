@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) {
        return;
    }
    let size = params.reserved.x;
    // Each remainder is smaller than INT32_MAX, so these additions fit u32.
    let carry_x = (id.x % size + u32(params.offsets.x)) / size;
    let carry_y = (id.y % size + u32(params.offsets.y)) / size;
    let parity = (id.x / size) ^ (id.y / size) ^ carry_x ^ carry_y ^ params.reserved.y;
    pixels[id.y * params.dimensions.x + id.x] =
        select(params.color1, params.color2, (parity & 1u) != 0u);
}
