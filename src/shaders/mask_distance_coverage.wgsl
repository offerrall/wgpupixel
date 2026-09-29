//! include selection_common
//! coverage none
//! entry none
// Converts nearest features into coverage. reserved: x operation (0 expand, 1 contract,
// 2 border), y canvas bounds. values.x radius. Plane 2 holds the nearest feature of
// the last transform (inside features, or outside ones for contract); border reads
// the outside ones from plane 3. The original coverage bytes are copied to word 0.
//
// A partially covered feature locates its piece of edge from its coverage and the
// coverage gradient (Gustavson and Strand's anti-aliased distance transform), which is
// exact for straight anti-aliased edges and within a few hundredths of a pixel on
// gentle curves. A fully covered feature is a pixel square seen as a disc of radius
// 0.5, as for hard selections. The moved edge keeps the normal it was measured with,
// and coverage is the exact area behind a straight edge at that distance (for discs,
// clamp(0.5 - distance)). Border is the expanded minus the contracted selection.
@group(0) @binding(0) var<storage, read> scratch: array<u32>;
@group(0) @binding(1) var<storage, read_write> destination: array<u32>;

fn level(x: i32, y: i32, outside: bool) -> f32 {
    let width = i32(params.dimensions.z);
    let index = u32(clamp(y, 0, i32(params.dimensions.w) - 1) * width + clamp(x, 0, width - 1));
    let value = f32(coverage_lane(scratch[index / 4u], index % 4u)) / 255.0;
    return select(value, 1.0 - value, outside);
}

// Signed distance from a pixel center to the edge through it (positive outside), for
// coverage a and gradient g (Gustavson and Strand 2011).
fn edge_offset(g: vec2<f32>, a: f32) -> f32 {
    if (g.x == 0.0 || g.y == 0.0) { return 0.5 - a; }
    let n = abs(normalize(g));
    let big = max(n.x, n.y);
    let small = min(n.x, n.y);
    let a1 = 0.5 * small / big;
    if (a < a1) { return 0.5 * (big + small) - sqrt(2.0 * big * small * a); }
    if (a < 1.0 - a1) { return (0.5 - a) * big; }
    return -0.5 * (big + small) + sqrt(2.0 * big * small * (1.0 - a));
}

fn gradient_at(q: vec2<i32>, outside: bool) -> vec2<f32> {
    let r2 = 1.41421356;
    return vec2<f32>(
        level(q.x + 1, q.y - 1, outside) + r2 * level(q.x + 1, q.y, outside) + level(q.x + 1, q.y + 1, outside) -
        level(q.x - 1, q.y - 1, outside) - r2 * level(q.x - 1, q.y, outside) - level(q.x - 1, q.y + 1, outside),
        level(q.x - 1, q.y + 1, outside) + r2 * level(q.x, q.y + 1, outside) + level(q.x + 1, q.y + 1, outside) -
        level(q.x - 1, q.y - 1, outside) - r2 * level(q.x, q.y - 1, outside) - level(q.x + 1, q.y - 1, outside));
}

// Distance from p to the edge as seen by pixel n, with the edge normal (zero when
// unknown): a disc of radius 0.5 when n is fully covered, the straight piece through
// n when partial (normal from the coverage gradient, offset from n's coverage,
// spanning the pixel), none when empty.
fn seen_distance(p: vec2<i32>, n: vec2<i32>, outside: bool) -> vec3<f32> {
    let none = vec3<f32>(3.0e38, 0.0, 0.0);
    if (n.x < 0 || n.y < 0 || n.x >= i32(params.dimensions.z) || n.y >= i32(params.dimensions.w)) {
        return none;
    }
    let a = level(n.x, n.y, outside);
    let offset = vec2<f32>(p - n);
    if (a >= 1.0) { return vec3<f32>(length(offset) - 0.5, 0.0, 0.0); }
    if (a <= 0.0) { return none; }
    let gradient = gradient_at(n, outside);
    if (length(gradient) < 1e-6) { return vec3<f32>(max(length(offset) + 0.5 - a, 0.0), 0.0, 0.0); }
    let normal = -normalize(gradient);
    let tangent = vec2<f32>(-normal.y, normal.x);
    let reach = 0.5 / max(abs(tangent.x), abs(tangent.y));
    // Relative to the edge point of n, which lies edge_offset inward along the normal.
    let relative = offset + normal * edge_offset(gradient, a);
    let along = clamp(dot(relative, tangent), -reach, reach);
    return vec3<f32>(length(relative - tangent * along), normal);
}

// Signed distance from pixel p to the selection edge (negative inside) and the edge
// normal there. The nearest feature by center can sit a few pixels along the edge from
// the true foot point, so the search walks from it through neighboring pixels while
// the edge gets closer.
fn edge_distance(plane: u32, p: vec2<i32>, outside: bool) -> vec3<f32> {
    let width = params.dimensions.z;
    let feature = scratch[plane * width * params.dimensions.w + u32(p.y) * width + u32(p.x)];
    if (feature == 0xffffffffu) { return vec3<f32>(3.0e38, 0.0, 0.0); }
    var center = vec2<i32>(i32(feature % width), i32(feature / width));
    var nearest = seen_distance(p, center, outside);
    for (var step = 0; step < 16; step++) {
        var next = center;
        for (var dy = -1; dy <= 1; dy++) {
            for (var dx = -1; dx <= 1; dx++) {
                let n = center + vec2<i32>(dx, dy);
                let seen = seen_distance(p, n, outside);
                if (seen.x < nearest.x) {
                    nearest = seen;
                    next = n;
                }
            }
        }
        if (all(next == center)) { break; }
        center = next;
    }
    // A partial pixel p itself lies inside when at least half covered.
    let own = level(p.x, p.y, outside);
    if (own >= 0.5 && own < 1.0) {
        let seen = seen_distance(p, p, outside);
        if (seen.x < nearest.x) { nearest = seen; }
        return vec3<f32>(-nearest.x, nearest.yz);
    }
    return nearest;
}

// Coverage of a pixel whose center is signed distance d outside a straight edge with
// the given normal: the inverse of edge_offset, or clamp(0.5 - d) without a normal.
fn edge_coverage(d: f32, normal: vec2<f32>) -> f32 {
    if (all(normal == vec2<f32>(0.0))) { return clamp(0.5 - d, 0.0, 1.0); }
    let n = abs(normal);
    let big = max(n.x, n.y);
    let small = min(n.x, n.y);
    let corner = 0.5 * (big + small);
    let ramp = 0.5 * (big - small);
    if (d >= corner) { return 0.0; }
    if (d <= -corner) { return 1.0; }
    if (small < 1e-6 || abs(d) <= ramp) { return clamp(0.5 - d / big, 0.0, 1.0); }
    // Finite even for axis-aligned normals (small == 0), whose branch returned above:
    // compilers may evaluate both sides of a branch, and Vulkan leaves results that an
    // infinity or NaN reaches undefined (llvmpipe on AVX-512 hosts turned axis-aligned
    // contracted edges back into their input).
    let area = 2.0 * big * max(small, 1e-6);
    if (d > 0.0) { return (corner - d) * (corner - d) / area; }
    return 1.0 - (corner + d) * (corner + d) / area;
}

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(workgroup_id) group: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>,
        @builtin(local_invocation_index) lane: u32) {
    let word = linear_index(group, groups, lane);
    let width = params.dimensions.z;
    let height = params.dimensions.w;
    let size = width * height;
    if (word >= (size + 3u) / 4u) { return; }
    let radius = params.values.x;
    let operation = params.reserved.x;
    let existing = destination[word];
    var packed = 0u;
    for (var k = 0u; k < 4u; k++) {
        let pixel = min(4u * word + k, size - 1u);
        let original = coverage_lane(existing, k);
        let p = vec2<i32>(i32(pixel % width), i32(pixel / width));
        var grown = 0.0;
        if (operation != 1u) {
            let inside = edge_distance(2u, p, false);
            grown = edge_coverage(inside.x - radius, inside.yz);
        }
        var shrunk = 1.0;
        if (operation != 0u) {
            var outside = edge_distance(select(2u, 3u, operation == 2u), p, true);
            shrunk = 1.0 - edge_coverage(outside.x - radius, outside.yz);
            if (params.reserved.y == 1u) {
                // Outside the canvas is unselected: intersect with the canvas rectangle
                // inset by the radius, by exact area.
                let low = vec2<f32>(p);
                let size = vec2<f32>(f32(width), f32(height));
                let overlap = clamp(min(low + 1.0, size - radius) - max(low, vec2<f32>(radius)),
                                    vec2<f32>(0.0), vec2<f32>(1.0));
                shrunk = min(shrunk, overlap.x * overlap.y);
            }
        }
        var value = original;
        switch operation {
            case 0u: { value = max(original, quantize(grown)); }
            case 1u: { value = min(original, quantize(shrunk)); }
            default: { value = quantize(grown - shrunk); }
        }
        packed |= value << (8u * k);
    }
    destination[word] = packed;
}
