//! coverage none
//! entry none
@group(0) @binding(0) var<storage, read_write> matrix: array<vec2<u32>>;
@group(0) @binding(4) var<storage, read> totals: array<u32>;
var<workgroup> scan: array<u32, 64>;
@compute @workgroup_size(8, 8)
fn main(@builtin(local_invocation_index) lane: u32) {
    let words = params.reserved.x;
    let blocks = (words + 63u) / 64u;
    let chunk = (blocks + 63u) / 64u;
    var prefix: array<u32, 32>;
    var sum = 0u;
    for (var i = 0u; i < chunk; i += 1u) {
        let block = lane * chunk + i;
        if (block < blocks) { sum += totals[block]; }
        prefix[i] = sum;
    }
    scan[lane] = sum;
    workgroupBarrier();
    for (var shift = 1u; shift < 64u; shift *= 2u) {
        var add = 0u;
        if (lane >= shift) { add = scan[lane - shift]; }
        workgroupBarrier();
        scan[lane] += add;
        workgroupBarrier();
    }
    let before = scan[lane] - sum;
    for (var i = 0u; i < chunk; i += 1u) {
        let block = lane * chunk + i;
        if (block < blocks) {
            let index = u32(params.offsets.w) + min((block + 1u) * 64u, words) - 1u;
            matrix[index].y = before + prefix[i];
        }
    }
}
