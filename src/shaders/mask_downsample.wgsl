//! include selection_common
//! coverage none
//! entry none
// Large feathers, step 1: mean coverage (0-255) of each factor x factor block into the
// float plane at word values.x. offsets: width, height, factor. The reduced grid has
// one extra cell on every side holding the canvas edge: outside the canvas the
// selection repeats its edge pixels, so that border cell is the mean of the edge
// pixels (one pixel deep) it faces, and the grid repeats it further out. With canvas
// bounds (reserved.x) outside is unselected: border cells are zero and partial blocks
// count the missing pixels as zero. Otherwise partial blocks repeat the edge too.
@group(0) @binding(0) var<storage, read_write> scratch: array<u32>;

fn block_range(cell: u32, cells: u32, factor: u32, extent: u32) -> vec2<u32> {
    // Border cells face the first or last line of pixels.
    if (cell == 0u) { return vec2<u32>(0u, 1u); }
    if (cell == cells - 1u) { return vec2<u32>(extent - 1u, extent); }
    let start = (cell - 1u) * factor;
    return vec2<u32>(start, start + factor);
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(workgroup_id) group: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>,
        @builtin(local_invocation_index) lane: u32) {
    let cell = linear_index(group, groups, lane);
    let width = u32(params.offsets.x);
    let height = u32(params.offsets.y);
    let factor = u32(params.offsets.z);
    let columns = (width + factor - 1u) / factor + 2u;
    let rows = (height + factor - 1u) / factor + 2u;
    if (cell >= columns * rows) { return; }
    let cx = cell % columns;
    let cy = cell / columns;
    let bounds = params.reserved.x == 1u;
    var mean = 0.0;
    let border = cx == 0u || cy == 0u || cx == columns - 1u || cy == rows - 1u;
    if (!(bounds && border)) {
        let xs = block_range(cx, columns, factor, width);
        let ys = block_range(cy, rows, factor, height);
        var sum = 0u;
        for (var y = ys.x; y < ys.y; y++) {
            for (var x = xs.x; x < xs.y; x++) {
                // Pixels past the canvas repeat the edge, or are zero with bounds.
                if (bounds && (x >= width || y >= height)) { continue; }
                let index = min(y, height - 1u) * width + min(x, width - 1u);
                sum += coverage_lane(scratch[index / 4u], index % 4u);
            }
        }
        mean = f32(sum) / f32((xs.y - xs.x) * (ys.y - ys.x));
    }
    scratch[bitcast<u32>(params.values.x) + cell] = bitcast<u32>(mean);
}
