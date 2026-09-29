// Final write coverage in destination coordinates, rounded once to A8.
fn mask_write_byte(index: u32, before: u32, after: u32) -> u32 {
    let width = params.dimensions.z;
    let x = i32(index % width);
    let y = i32(index / width);
    if (index >= width * params.dimensions.w || x < params.offsets.x || y < params.offsets.y ||
        x >= params.offsets.z || y >= params.offsets.w) { return before; }
    return u32(floor(mix(f32(before), f32(after), operation_coverage(index)) + 0.5));
}
