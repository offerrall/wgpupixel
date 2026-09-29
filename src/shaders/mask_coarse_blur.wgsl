//! include selection_common
//! coverage none
//! entry none
// Large feathers, step 2: exact Gaussian pass along reserved.x (0 rows, 1 columns) of
// the coarse float plane at word values.x into values.y. offsets: coarse width and
// height. reserved.y: taps; reserved.z: canvas bounds (outside is zero, else the
// edge repeats). data: weights for distances 0..taps.
@group(0) @binding(0) var<storage, read_write> scratch: array<u32>;
@group(0) @binding(7) var<storage, read> data: array<f32>;

fn coarse_at(x: i32, y: i32) -> f32 {
    let width = params.offsets.x;
    let height = params.offsets.y;
    if (params.reserved.z == 1u && (x < 0 || y < 0 || x >= width || y >= height)) { return 0.0; }
    let index = u32(clamp(y, 0, height - 1) * width + clamp(x, 0, width - 1));
    return bitcast<f32>(scratch[bitcast<u32>(params.values.x) + index]);
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(workgroup_id) group: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>,
        @builtin(local_invocation_index) lane: u32) {
    let cell = linear_index(group, groups, lane);
    let width = u32(params.offsets.x);
    if (cell >= width * u32(params.offsets.y)) { return; }
    let x = i32(cell % width);
    let y = i32(cell / width);
    let step = select(vec2<i32>(1, 0), vec2<i32>(0, 1), params.reserved.x == 1u);
    var sum = data[0] * coarse_at(x, y);
    for (var d = 1; d <= i32(params.reserved.y); d++) {
        sum += data[d] * (coarse_at(x - d * step.x, y - d * step.y) + coarse_at(x + d * step.x, y + d * step.y));
    }
    scratch[bitcast<u32>(params.values.y) + cell] = bitcast<u32>(sum);
}
