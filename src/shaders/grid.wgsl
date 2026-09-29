@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) {
        return;
    }
    let spacing = params.reserved.x;
    // Reduced coordinates plus reduced offsets fit u32, including INT_MAX spacing.
    let x = (id.x % spacing + u32(params.offsets.x)) % spacing;
    let y = (id.y % spacing + u32(params.offsets.y)) % spacing;
    let line = x < params.reserved.y || y < params.reserved.y;
    pixels[id.y * params.dimensions.x + id.x] = select(params.color2, params.color1, line);
}
