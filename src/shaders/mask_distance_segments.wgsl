//! include selection_common
//! coverage none
//! entry none
// Pass 2: minimum column distance of each run of 16 pixels in a row, so pass 3 can
// skip runs that cannot hold a nearer feature. Written to scratch plane 1; runs
// without features hold 0xffffff.
@group(0) @binding(0) var<storage, read_write> scratch: array<u32>;

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(workgroup_id) group: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>,
        @builtin(local_invocation_index) lane: u32) {
    let invocation = linear_index(group, groups, lane);
    let width = u32(params.offsets.x);
    let height = u32(params.offsets.y);
    let segments = (width + 15u) / 16u;
    if (invocation >= segments * height) { return; }
    let y = invocation / segments;
    let start = (invocation % segments) * 16u;
    var smallest = 0x7fffffu;
    for (var x = start; x < min(start + 16u, width); x++) {
        smallest = min(smallest, scratch[y * width + x] >> 9u);
    }
    scratch[width * height + invocation] = smallest;
}
