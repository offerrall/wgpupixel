//! include selection_common
//! coverage none
//! entry none
// Vertical pass reading the 16-bit sums of mask_filter_horizontal (scratch word
// reserved.w) and combining the result into the selection with mode offsets.z.
// Box sums become a majority vote with a one-pixel anti-aliased ramp.
@group(0) @binding(0) var<storage, read> scratch: array<u32>;
@group(0) @binding(1) var<storage, read_write> destination: array<u32>;
@group(0) @binding(7) var<storage, read> data: array<f32>;

fn sum_at(x: u32, y: i32, width: u32, height: i32) -> u32 {
    if (params.reserved.z == 1u && (y < 0 || y >= height)) { return 0u; }
    let index = u32(clamp(y, 0, height - 1)) * width + x;
    let word = scratch[params.reserved.w + index / 2u];
    return (word >> (16u * (index % 2u))) & 65535u;
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(workgroup_id) group: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>,
        @builtin(local_invocation_index) lane: u32) {
    let word = linear_index(group, groups, lane);
    let width = u32(params.offsets.x);
    let height = params.offsets.y;
    let count = width * u32(height);
    if (word >= (count + 3u) / 4u) { return; }
    let radius = i32(params.reserved.y);
    var incoming = vec4<u32>(0u);
    for (var k = 0u; k < 4u; k++) {
        let pixel = min(4u * word + k, count - 1u);
        let x = pixel % width;
        let y = i32(pixel / width);
        let center = sum_at(x, y, width, height);
        var coverage = 0.0;
        if (params.reserved.x == 1u) {
            var total = center;
            for (var d = 1; d <= radius; d++) {
                total += sum_at(x, y - d, width, height) + sum_at(x, y + d, width, height);
            }
            let side = f32(2 * radius + 1);
            let mean = f32(total) / (side * side * 255.0);
            coverage = (mean - 0.5) * side + 0.5;
        } else {
            var weighted = data[0] * f32(center);
            for (var d = 1; d <= radius; d++) {
                let pair = sum_at(x, y - d, width, height) + sum_at(x, y + d, width, height);
                weighted += data[d] * f32(pair);
            }
            coverage = weighted / 65535.0;
        }
        incoming[k] = quantize(coverage);
    }
    destination[word] = combine_word(destination[word], incoming, u32(params.offsets.z));
}
