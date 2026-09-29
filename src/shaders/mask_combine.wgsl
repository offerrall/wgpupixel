//! include selection_common mask_write
//! coverage kernel
//! entry none
// destination = SelectionMode reserved.x of (destination, source), one word each.
@group(0) @binding(0) var<storage, read> source: array<u32>;
@group(0) @binding(1) var<storage, read_write> destination: array<u32>;

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(workgroup_id) group: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>,
        @builtin(local_invocation_index) lane: u32) {
    let word = linear_index(group, groups, lane);
    if (word >= (params.dimensions.z * params.dimensions.w + 3u) / 4u) { return; }
    let incoming = source[word];
    let before = destination[word];
    var packed = 0u;
    for (var k = 0u; k < 4u; k++) {
        let byte = coverage_lane(before, k);
        let result = combine_coverage(byte, coverage_lane(incoming, k), params.reserved.x);
        packed |= mask_write_byte(word * 4u + k, byte, result) << (k * 8u);
    }
    destination[word] = packed;
}
