//! include selection_common
//! coverage none
//! entry none
// Pass 3: nearest feature in the plane, min over the row of dx^2 + g^2, one pixel per
// invocation in exact integer arithmetic; ties keep the more covered feature. The scan
// walks outward and stops once dx alone cannot beat the best or exceeds the cap
// reserved.y; runs whose minimum column distance cannot improve the result are skipped
// whole. Writes the feature's pixel index, or far, to scratch plane reserved.x. The
// cap keeps (2 cap^2) << 8 within 32 bits.
@group(0) @binding(0) var<storage, read_write> scratch: array<u32>;

const far = 0xffffffffu;

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(workgroup_id) group: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>,
        @builtin(local_invocation_index) lane: u32) {
    let pixel = linear_index(group, groups, lane);
    let width = i32(params.offsets.x);
    let size = u32(width) * u32(params.offsets.y);
    if (pixel >= size) { return; }
    let cap = i32(params.reserved.y);
    let x = i32(pixel % u32(width));
    let row = pixel - u32(x);
    let segments = size + pixel / u32(width) * u32((width + 15) / 16);
    var best = far;
    var nearest = far;
    for (var side = 0; side < 2; side++) {
        let step = select(-1, 1, side == 0);
        var i = select(x - 1, x, side == 0);
        var entered = true;
        loop {
            if (i < 0 || i >= width) { break; }
            let dx = abs(i - x);
            if (dx > cap || (u32(dx * dx) << 8u) >= best) { break; }
            if (entered || i % 16 == select(15, 0, side == 0)) {
                entered = false;
                let floor_distance = scratch[segments + u32(i / 16)];
                if (floor_distance == 0x7fffffu ||
                    ((u32(dx * dx) + floor_distance * floor_distance) << 8u) >= best) {
                    // Jump past this run in the scan direction.
                    i = select(i / 16 * 16 - 1, i / 16 * 16 + 16, side == 0);
                    continue;
                }
            }
            let column = scratch[row + u32(i)];
            if (column != far) {
                let g = column >> 9u;
                let candidate = ((u32(dx * dx) + g * g) << 8u) | ((column >> 1u) & 255u);
                if (candidate < best) {
                    best = candidate;
                    let row_of = select(pixel / u32(width) - g, pixel / u32(width) + g, (column & 1u) == 1u);
                    nearest = row_of * u32(width) + u32(i);
                }
            }
            i += step;
        }
    }
    scratch[params.reserved.x * size + pixel] = nearest;
}
