//! include selection_common
//! coverage none
//! entry none
// One 8x8 workgroup: rounded mean of the straight 8-bit pixels in the square of radius
// reserved.x around offsets.xy (clipped to the image), stored after the labels.
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> scratch: array<u32>;

var<workgroup> sums: array<vec4<i32>, 64>;

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(local_invocation_index) lane: u32) {
    let width = i32(params.dimensions.x);
    let height = i32(params.dimensions.y);
    let radius = i32(params.reserved.x);
    let left = max(params.offsets.x - radius, 0);
    let top = max(params.offsets.y - radius, 0);
    let columns = u32(min(params.offsets.x + radius, width - 1) - left + 1);
    let rows = u32(min(params.offsets.y + radius, height - 1) - top + 1);
    var sum = vec4<i32>(0);
    for (var i = lane; i < columns * rows; i += 64u) {
        let x = u32(left) + i % columns;
        let y = u32(top) + i / columns;
        sum += straight_bytes(source[y * u32(width) + x]);
    }
    sums[lane] = sum;
    workgroupBarrier();
    if (lane != 0u) { return; }
    var total = vec4<i32>(0);
    for (var i = 0u; i < 64u; i++) { total += sums[i]; }
    let count = i32(columns * rows);
    let labels = params.dimensions.x * params.dimensions.y;
    // Rounded mean, half up, with floor division for negative sums.
    let numerator = 2 * total + vec4<i32>(count);
    let denominator = 2 * count;
    var mean = numerator / denominator;
    mean -= select(vec4<i32>(0), vec4<i32>(1), (numerator < vec4<i32>(0)) & (mean * denominator != numerator));
    scratch[labels] = bitcast<u32>(mean.r);
    scratch[labels + 1u] = bitcast<u32>(mean.g);
    scratch[labels + 2u] = bitcast<u32>(mean.b);
    scratch[labels + 3u] = bitcast<u32>(mean.a);
}
