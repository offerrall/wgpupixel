//! include wand_union
//! coverage none
//! entry none
// Magic wand, step 2: joins tile components across tile borders. offsets.xy: image
// size; reserved.x: 8-connected.
@group(0) @binding(0) var<storage, read_write> labels: array<atomic<u32>>;

const none = 0xffffffffu;

fn connect(index: u32, neighbor: u32) {
    if (atomicLoad(&labels[neighbor]) != none) { unite(index, neighbor); }
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let width = u32(params.offsets.x);
    if (id.x >= width || id.y >= u32(params.offsets.y)) { return; }
    let x = id.x;
    let y = id.y;
    let index = y * width + x;
    let left = x % 16u == 0u;
    let right = x % 16u == 15u;
    let top = y % 16u == 0u;
    if (!(left || top || (right && params.reserved.x == 1u))) { return; }
    if (atomicLoad(&labels[index]) == none) { return; }
    if (left && x > 0u) { connect(index, index - 1u); }
    if (y == 0u) { return; }
    if (top) { connect(index, index - width); }
    if (params.reserved.x == 1u) {
        if (x > 0u && (left || top)) { connect(index, index - width - 1u); }
        if (x + 1u < width && (right || top)) { connect(index, index - width + 1u); }
    }
}
