//! include selection_common
//! coverage none
//! entry none
// Exact Euclidean distance transform, pass 1: for every pixel, the vertical distance g
// to the nearest feature pixel in its column (up to cap reserved.y, else far), packed
// as (g << 9) | ((255 - level) << 1) | below, so the minimum prefers the nearer
// feature, then the more covered one; below says whether it lies below the pixel. Each invocation sweeps a run of 64 rows
// of one column, first looking up to cap rows past both ends of its run.
// The level is the coverage byte, or its complement when reserved.x is 1 (outside).
// Features carry the sub-pixel edge: pixels at level >= 128, and nonzero pixels next
// to one (the anti-aliased rim). Far feathered tails are not features.
@group(0) @binding(0) var<storage, read> mask: array<u32>;
@group(0) @binding(1) var<storage, read_write> scratch: array<u32>;

const far = 0xffffffffu;
const none = 256u;

fn level(x: u32, y: u32) -> u32 {
    let index = y * params.dimensions.x + x;
    let value = coverage_lane(mask[index / 4u], index % 4u);
    return select(value, 255u - value, params.reserved.x == 1u);
}

// 255 - level of a feature pixel, or none.
fn feature(x: u32, y: u32) -> u32 {
    let value = level(x, y);
    if (value >= 128u) { return 255u - value; }
    if (value == 0u) { return none; }
    for (var dy = -1; dy <= 1; dy++) {
        for (var dx = -1; dx <= 1; dx++) {
            let nx = i32(x) + dx;
            let ny = i32(y) + dy;
            if (nx < 0 || ny < 0 || nx >= i32(params.dimensions.x) || ny >= i32(params.dimensions.y)) {
                continue;
            }
            if (level(u32(nx), u32(ny)) >= 128u) { return 255u - value; }
        }
    }
    return none;
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(workgroup_id) group: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>,
        @builtin(local_invocation_index) lane: u32) {
    let invocation = linear_index(group, groups, lane);
    let width = params.dimensions.x;
    let height = params.dimensions.y;
    let cap = params.reserved.y;
    if (invocation >= width * ((height + 63u) / 64u)) { return; }
    let x = invocation % width;
    let first = invocation / width * 64u;
    let last = min(first + 64u, height);
    // Nearest feature row above (stored + 1 so zero means none) and its weight.
    var above = 0u;
    var above_weight = 0u;
    for (var y = first; y > 0u && first - y < cap; y--) {
        let weight = feature(x, y - 1u);
        if (weight != none) {
            above = y;
            above_weight = weight;
            break;
        }
    }
    for (var y = first; y < last; y++) {
        let weight = feature(x, y);
        if (weight != none) {
            above = y + 1u;
            above_weight = weight;
        }
        var value = far;
        if (above > 0u && y + 1u - above <= cap) { value = ((y + 1u - above) << 9u) | (above_weight << 1u); }
        scratch[y * width + x] = value;
    }
    var below = far;
    var below_weight = 0u;
    for (var y = last; y < height && y - last < cap; y++) {
        let weight = feature(x, y);
        if (weight != none) {
            below = y;
            below_weight = weight;
            break;
        }
    }
    for (var y = last; y > first; y--) {
        let index = (y - 1u) * width + x;
        let current = scratch[index];
        if (current != far && current >> 9u == 0u) {
            below = y - 1u;
            below_weight = (current >> 1u) & 255u;
        }
        if (below != far && below - (y - 1u) <= cap) {
            scratch[index] = min(current, ((below - (y - 1u)) << 9u) | (below_weight << 1u) | 1u);
        }
    }
}
