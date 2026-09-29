//! include selection_common
//! coverage none
//! entry none
// Large feathers, step 3: bilinear reconstruction of the blurred coarse plane at word
// values.x (block centers; border cells sit half a block outside) at every pixel, combined into the selection with mode reserved.x, one word
// per invocation. offsets: width, height, factor.
@group(0) @binding(0) var<storage, read> scratch: array<u32>;
@group(0) @binding(1) var<storage, read_write> destination: array<u32>;

fn coarse(x: i32, y: i32, columns: i32, rows: i32) -> f32 {
    let index = u32(clamp(y, 0, rows - 1) * columns + clamp(x, 0, columns - 1));
    return bitcast<f32>(scratch[bitcast<u32>(params.values.x) + index]);
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(workgroup_id) group: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>,
        @builtin(local_invocation_index) lane: u32) {
    let word = linear_index(group, groups, lane);
    let width = u32(params.offsets.x);
    let count = width * u32(params.offsets.y);
    if (word >= (count + 3u) / 4u) { return; }
    let factor = f32(params.offsets.z);
    // The reduced grid includes one border cell on each side.
    let columns = (params.offsets.x + params.offsets.z - 1) / params.offsets.z + 2;
    let rows = (params.offsets.y + params.offsets.z - 1) / params.offsets.z + 2;
    var incoming = vec4<u32>(0u);
    for (var k = 0u; k < 4u; k++) {
        let pixel = min(4u * word + k, count - 1u);
        let position = (vec2<f32>(f32(pixel % width), f32(pixel / width)) + 0.5) / factor + 0.5;
        let base = floor(position);
        let t = position - base;
        let i = vec2<i32>(base);
        let top = mix(coarse(i.x, i.y, columns, rows), coarse(i.x + 1, i.y, columns, rows), t.x);
        let bottom = mix(coarse(i.x, i.y + 1, columns, rows), coarse(i.x + 1, i.y + 1, columns, rows), t.x);
        incoming[k] = quantize(mix(top, bottom, t.y) / 255.0);
    }
    destination[word] = combine_word(destination[word], incoming, params.reserved.x);
}
