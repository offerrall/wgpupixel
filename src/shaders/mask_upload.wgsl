//! include selection_common
//! coverage none
//! entry none
@group(0) @binding(0) var<storage, read> source: array<u32>;
@group(0) @binding(1) var<storage, read_write> destination: array<u32>;
@compute @workgroup_size(8, 8, 1)
fn main(@builtin(workgroup_id) group: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>,
        @builtin(local_invocation_index) lane: u32) {
    let word = linear_index(group, groups, lane);
    let count = params.dimensions.z * params.dimensions.w;
    if (word >= (count + 3u) / 4u) { return; }
    let before = destination[word];
    var packed = 0u;
    for (var k = 0u; k < 4u; k++) {
        let index = word * 4u + k;
        var byte = coverage_lane(before, k);
        let x = index % params.dimensions.z;
        let y = index / params.dimensions.z;
        let origin = vec2<u32>(params.offsets.xy);
        if (x >= origin.x && y >= origin.y && x - origin.x < params.dimensions.x && y - origin.y < params.dimensions.y) {
            let source_index = (y - origin.y) * params.dimensions.x + x - origin.x;
            byte = coverage_lane(source[source_index / 4u], source_index % 4u);
        }
        packed |= byte << (k * 8u);
    }
    destination[word] = packed;
}
