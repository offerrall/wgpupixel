//! include selection_common
//! coverage none
//! entry none
// Magic wand, step 4: coverage from labels. reserved: x mode, y contiguous (only the
// seed's component), z anti-alias, w seed index. Anti-aliasing blends a 3x3 tent of
// the matched set into the half of the range on the pixel's own side of 50%.
@group(0) @binding(0) var<storage, read> scratch: array<u32>;
@group(0) @binding(1) var<storage, read_write> destination: array<u32>;

fn matched(x: i32, y: i32, root: u32) -> f32 {
    let width = i32(params.dimensions.z);
    let height = i32(params.dimensions.w);
    let label = scratch[u32(clamp(y, 0, height - 1) * width + clamp(x, 0, width - 1))];
    if (params.reserved.y == 1u) {
        return select(0.0, 1.0, label == root && root != 0xffffffffu);
    }
    return select(0.0, 1.0, label != 0xffffffffu);
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(workgroup_id) group: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>,
        @builtin(local_invocation_index) lane: u32) {
    let word = linear_index(group, groups, lane);
    let width = params.dimensions.z;
    let count = width * params.dimensions.w;
    if (word >= (count + 3u) / 4u) { return; }
    let root = scratch[params.reserved.w];
    var incoming = vec4<u32>(0u);
    for (var k = 0u; k < 4u; k++) {
        let pixel = min(4u * word + k, count - 1u);
        let x = i32(pixel % width);
        let y = i32(pixel / width);
        let center = matched(x, y, root);
        var coverage = center;
        if (params.reserved.z == 1u) {
            var tent = 0.0;
            for (var dy = -1; dy <= 1; dy++) {
                for (var dx = -1; dx <= 1; dx++) {
                    tent += f32((2 - abs(dx)) * (2 - abs(dy))) * matched(x + dx, y + dy, root);
                }
            }
            coverage = 0.5 * center + 0.5 * tent / 16.0;
        }
        incoming[k] = quantize(coverage);
    }
    destination[word] = combine_word(destination[word], incoming, params.reserved.x);
}
