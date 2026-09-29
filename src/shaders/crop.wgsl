@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;

// Returns extent as an out-of-bounds sentinel. Avoid signed addition and
// negation of INT32_MIN, including for origins at either integer extreme.
fn crop_coordinate(coordinate: u32, origin: i32, extent: u32) -> u32 {
    if (origin < 0) {
        let magnitude = u32(-(origin + 1)) + 1u;
        if (coordinate < magnitude) {
            return extent;
        }
        return min(coordinate - magnitude, extent);
    }
    let offset = u32(origin);
    if (offset >= extent) {
        return extent;
    }
    if (coordinate >= extent - offset) {
        return extent;
    }
    return coordinate + offset;
}

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.z || id.y >= params.dimensions.w) {
        return;
    }
    let x = crop_coordinate(id.x, params.offsets.x, params.dimensions.x);
    let y = crop_coordinate(id.y, params.offsets.y, params.dimensions.y);
    let index = id.y * params.dimensions.z + id.x;
    if (x >= params.dimensions.x || y >= params.dimensions.y) {
        destination[index] = vec4<f32>(0.0);
        return;
    }
    destination[index] = source[y * params.dimensions.x + x];
}
