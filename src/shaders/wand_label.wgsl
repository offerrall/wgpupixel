//! include selection_common
//! coverage none
//! entry none
// Magic wand, step 1: each 16x16 tile labels its connected matching pixels with
// union-find in workgroup memory. A label is the image index of the component's
// first pixel in the tile; non-matching pixels get none. values.x: tolerance;
// reserved.x: 8-connected. The reference color follows the labels in scratch.
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> scratch: array<u32>;

const none = 0xffffffffu;
var<workgroup> parents: array<atomic<u32>, 256>;

fn find_local(start: u32) -> u32 {
    var node = start;
    var parent = atomicLoad(&parents[node]);
    while (parent != node) {
        node = parent;
        parent = atomicLoad(&parents[node]);
    }
    return node;
}

fn unite_local(first: u32, second: u32) {
    var a = find_local(first);
    var b = find_local(second);
    loop {
        if (a == b) { return; }
        if (a < b) {
            let previous = atomicMin(&parents[b], a);
            if (previous == b) { return; }
            b = find_local(previous);
        } else {
            let previous = atomicMin(&parents[a], b);
            if (previous == a) { return; }
            a = find_local(previous);
        }
    }
}

fn connect(lane: u32, neighbor: u32) {
    if (atomicLoad(&parents[neighbor]) != none) { unite_local(lane, neighbor); }
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(workgroup_id) group: vec3<u32>, @builtin(local_invocation_id) local: vec3<u32>,
        @builtin(local_invocation_index) lane: u32) {
    let width = params.dimensions.x;
    let height = params.dimensions.y;
    let x = group.x * 16u + local.x;
    let y = group.y * 16u + local.y;
    let labels = width * height;
    let reference = bitcast<vec4<i32>>(vec4<u32>(scratch[labels], scratch[labels + 1u],
                                                 scratch[labels + 2u], scratch[labels + 3u]));
    var inside = false;
    if (x < width && y < height) {
        let bytes = straight_bytes(source[y * width + x]);
        let difference = max(bytes, reference) - min(bytes, reference);
        let largest = max(max(difference.r, difference.g), max(difference.b, difference.a));
        inside = f32(largest) <= params.values.x;
    }
    atomicStore(&parents[lane], select(none, lane, inside));
    workgroupBarrier();
    if (inside) {
        if (local.x > 0u) { connect(lane, lane - 1u); }
        if (local.y > 0u) {
            connect(lane, lane - 16u);
            if (params.reserved.x == 1u && local.x > 0u) { connect(lane, lane - 17u); }
            if (params.reserved.x == 1u && local.x < 15u) { connect(lane, lane - 15u); }
        }
    }
    workgroupBarrier();
    if (x >= width || y >= height) { return; }
    var label = none;
    if (inside) {
        let root = find_local(lane);
        label = (group.y * 16u + root / 16u) * width + group.x * 16u + root % 16u;
    }
    scratch[y * width + x] = label;
}
