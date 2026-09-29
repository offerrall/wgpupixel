//! include selection_common
//! coverage none
//! entry none
// Horizontal pass of a separable selection filter inside the scratch mask: coverage
// bytes at word 0 -> 16-bit sums at word reserved.w, two pixels per invocation.
// offsets.xy: selection size. reserved: x kind (0 Gaussian with data weights for
// distances 0..radius, 1 box sum), y radius, z canvas bounds (outside is unselected).
@group(0) @binding(0) var<storage, read_write> scratch: array<u32>;
@group(0) @binding(7) var<storage, read> data: array<f32>;

fn coverage_at(row: u32, x: i32, width: i32) -> u32 {
    if (params.reserved.z == 1u && (x < 0 || x >= width)) { return 0u; }
    let index = row + u32(clamp(x, 0, width - 1));
    return coverage_lane(scratch[index / 4u], index % 4u);
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(workgroup_id) group: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>,
        @builtin(local_invocation_index) lane: u32) {
    let word = linear_index(group, groups, lane);
    let width = params.offsets.x;
    let count = u32(width) * u32(params.offsets.y);
    if (word >= (count + 1u) / 2u) { return; }
    let radius = i32(params.reserved.y);
    var packed = 0u;
    for (var k = 0u; k < 2u; k++) {
        let pixel = min(2u * word + k, count - 1u);
        let x = i32(pixel % u32(width));
        let row = pixel - u32(x);
        let center = coverage_at(row, x, width);
        var total = center;
        if (params.reserved.x == 1u) {
            for (var d = 1; d <= radius; d++) {
                total += coverage_at(row, x - d, width) + coverage_at(row, x + d, width);
            }
        } else {
            var weighted = data[0] * f32(center);
            for (var d = 1; d <= radius; d++) {
                let pair = coverage_at(row, x - d, width) + coverage_at(row, x + d, width);
                weighted += data[d] * f32(pair);
            }
            total = u32(floor(clamp(weighted / 255.0, 0.0, 1.0) * 65535.0 + 0.5));
        }
        packed |= total << (16u * k);
    }
    scratch[params.reserved.w + word] = packed;
}
