//! include selection_common mask_write
//! coverage kernel
//! entry none
// Maps every coverage byte through a 256-entry table packed four per word.
@group(0) @binding(0) var<storage, read_write> pixels: array<u32>;
@group(0) @binding(7) var<storage, read> data: array<u32>;

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(workgroup_id) group: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>,
        @builtin(local_invocation_index) lane: u32) {
    let word = linear_index(group, groups, lane);
    if (word >= (params.dimensions.x * params.dimensions.y + 3u) / 4u) { return; }
    let value = pixels[word];
    var packed = 0u;
    for (var k = 0u; k < 4u; k++) {
        let byte = coverage_lane(value, k);
        packed |= mask_write_byte(word * 4u + k, byte, coverage_lane(data[byte / 4u], byte % 4u)) << (k * 8u);
    }
    pixels[word] = packed;
}
