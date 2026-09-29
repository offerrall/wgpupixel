//! coverage none
//! entry none
// One invocation per packed word in each rectangle row. Interior words need only
// one store; partial end words use atomic read/modify/write to preserve neighbouring
// bytes, including bytes owned by another invocation on an odd-width canvas row.
@group(0) @binding(0) var<storage, read> source: array<u32>;
@group(0) @binding(1) var<storage, read_write> destination: array<atomic<u32>>;

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) local: vec3<u32>) {
    if (local.y >= params.dispatch.w || params.dispatch.z == 0u) { return; }
    let begin = (params.dispatch.y + local.y) * params.dimensions.z + params.dispatch.x;
    let end = begin + params.dispatch.z;
    let word = begin / 4u + local.x;
    if (word > (end - 1u) / 4u) { return; }
    let byte = word * 4u;
    let first = max(begin, byte) - byte;
    let last = min(end - byte, 4u);
    if (first == 0u && last == 4u) {
        // The atomic-typed binding requires atomicStore, but no read/modify/write
        // is needed: this invocation owns all four bytes.
        atomicStore(&destination[word], source[word]);
    } else {
        let mask = (0xffffffffu << (first * 8u)) & (0xffffffffu >> ((4u - last) * 8u));
        atomicAnd(&destination[word], ~mask);
        atomicOr(&destination[word], source[word] & mask);
    }
}
