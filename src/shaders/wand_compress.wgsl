//! include wand_union
//! coverage none
//! entry none
// Magic wand, step 3: points every matching pixel directly at its component root.
@group(0) @binding(0) var<storage, read_write> labels: array<atomic<u32>>;

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let width = u32(params.offsets.x);
    if (id.x >= width || id.y >= u32(params.offsets.y)) { return; }
    let index = id.y * width + id.x;
    if (atomicLoad(&labels[index]) != 0xffffffffu) {
        atomicStore(&labels[index], find(index));
    }
}
