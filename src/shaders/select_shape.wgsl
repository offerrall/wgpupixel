//! include selection_common
//! coverage none
//! entry none
// Polygon coverage of a width x height (offsets.xy) selection in mask_words. Each
// 256-invocation workgroup computes one row, in runs of 256 pixels. reserved: x mode, y fill rule (1 even-odd), z anti-alias. data: edge pieces
// (x0, y0, x1, y1) cut at row boundaries, sorted by top. Horizontal pieces never
// change the winding but mark the pixels they pass through.
//
// The workgroup sorts the crossings of its row's center line, so a pixel no piece
// passes through reads its constant center winding by binary search. It also lists,
// per pixel, the pieces passing through it, and the pieces with a vertex inside the
// row. An edge pixel then works only on those (interval_coverage): exact unless two
// edges cross inside one 1/16-row slab of the pixel, and independent of contour
// orientation, overlap and duplication. Edge pixels are shared out over the whole
// workgroup. When a list overflows, the pixel scans every piece of the row instead;
// pixels met by more than 64 pieces, or by more than 32 winding changes left of them,
// are supersampled with exact fill rules on a 16x16 grid. Rows crossed by more pieces
// than workgroup memory holds read them from storage, with the same results.
@group(0) @binding(0) var<storage, read_write> mask_words: array<atomic<u32>>;
@group(0) @binding(7) var<storage, read> data: array<f32>;

const band_capacity = 256u;
const list_capacity = 1536u; // Packed two per word.
const vertex_capacity = 64u;
const slabs = 16u;
var<workgroup> band_edges: array<vec4<f32>, 256>;
var<workgroup> band_count: atomic<u32>;
var<workgroup> band_state: vec4<u32>; // range start, range end, pieces, winding sum
var<workgroup> crossing_count: atomic<u32>;
var<workgroup> crossing_x: array<f32, 256>;
var<workgroup> crossing_direction: array<i32, 256>;
var<workgroup> sorted_x: array<f32, 256>;
var<workgroup> winding_before: array<i32, 256>;
// Per pixel of the run: pieces through it, then (after the prefix sum) fill cursors.
var<workgroup> touched: array<atomic<u32>, 256>;
var<workgroup> list_start: array<u32, 256>;
var<workgroup> list_total: u32;
var<workgroup> piece_list: array<atomic<u32>, 768>;
var<workgroup> vertex_pieces: array<u32, 64>;
var<workgroup> vertex_count: atomic<u32>;
var<workgroup> pixel_bytes: array<u32, 256>;
var<workgroup> edge_pixels: array<u32, 256>;
var<workgroup> edge_pixel_count: atomic<u32>;
var<private> global_mode: bool;
var<private> global_first: u32;
// The edge pixel being resolved: pieces through it and winding changes along its left
// side (row, change), filled by the builders below.
var<private> near: array<u32, 64>;
var<private> near_count: u32;
var<private> change_rows: array<f32, 32>;
var<private> changes: array<i32, 32>;
var<private> change_count: u32;

fn edge_at(index: u32) -> vec4<f32> {
    return vec4<f32>(data[4u * index], data[4u * index + 1u], data[4u * index + 2u],
                     data[4u * index + 3u]);
}

// Piece i of this workgroup's row: from workgroup memory, or from storage when the
// row holds too many.
fn candidate(i: u32) -> vec4<f32> {
    if (global_mode) { return edge_at(global_first + i); }
    return band_edges[i];
}

fn ramp_integral(u: f32) -> f32 {
    if (u <= 0.0) { return 0.0; }
    if (u >= 1.0) { return u - 0.5; }
    return 0.5 * u * u;
}

fn x_at(edge: vec4<f32>, y: f32) -> f32 {
    return edge.x + (y - edge.y) * (edge.z - edge.x) / (edge.w - edge.y);
}

// Area of the unit-wide column at corner_x, between rows top and bottom (inside the
// edge's span), that lies right of the edge.
fn right_area(edge: vec4<f32>, corner_x: f32, top: f32, bottom: f32) -> f32 {
    let upper = corner_x + 1.0 - x_at(edge, top);
    let lower = corner_x + 1.0 - x_at(edge, bottom);
    let height = bottom - top;
    if (min(upper, lower) >= 1.0) { return height; }
    if (max(upper, lower) <= 0.0) { return 0.0; }
    if (abs(upper - lower) < 1e-3) { return height * clamp(0.5 * (upper + lower), 0.0, 1.0); }
    return height * (ramp_integral(upper) - ramp_integral(lower)) / (upper - lower);
}

// Whether a non-horizontal edge crosses the horizontal line at y (half-open in y).
fn spans(edge: vec4<f32>, y: f32) -> bool {
    return edge.y != edge.w && y >= min(edge.y, edge.w) && y < max(edge.y, edge.w);
}

fn direction(edge: vec4<f32>) -> i32 {
    return select(-1, 1, edge.w > edge.y);
}

fn inside(winding: i32) -> f32 {
    if (params.reserved.y == 1u) { return f32(winding & 1); }
    return select(0.0, 1.0, winding != 0);
}

// The x range of the edge within the row starting at top, or an empty range.
fn row_extent(edge: vec4<f32>, top: f32) -> vec2<f32> {
    if (edge.y == edge.w) {
        if (edge.y > top && edge.y < top + 1.0) { return vec2<f32>(min(edge.x, edge.z), max(edge.x, edge.z)); }
        return vec2<f32>(1.0, 0.0);
    }
    let upper = max(min(edge.y, edge.w), top);
    let lower = min(max(edge.y, edge.w), top + 1.0);
    if (lower <= upper) { return vec2<f32>(1.0, 0.0); }
    let a = x_at(edge, upper);
    let b = x_at(edge, lower);
    return vec2<f32>(min(a, b), max(a, b));
}

// Adds the piece to the near list when it passes through the pixel; false on overflow.
fn add_near(i: u32, corner: vec2<f32>) -> bool {
    let edge = candidate(i);
    let extent = row_extent(edge, corner.y);
    if (edge.y == edge.w || extent.x > extent.y) { return true; }
    if (extent.x >= corner.x + 1.0 || extent.y <= corner.x) { return true; }
    if (near_count == 64u) { return false; }
    near[near_count] = i;
    near_count += 1u;
    return true;
}

// Adds a winding change at row; changes that cancel free their slot. False on overflow.
fn add_change(row: f32, change: i32) -> bool {
    var slot = 0u;
    while (slot < change_count && change_rows[slot] != row) { slot++; }
    if (slot == change_count) {
        if (change_count == 32u) { return false; }
        change_rows[slot] = row;
        changes[slot] = 0;
        change_count += 1u;
    }
    changes[slot] += change;
    if (changes[slot] == 0) {
        change_count -= 1u;
        change_rows[slot] = change_rows[change_count];
        changes[slot] = changes[change_count];
    }
    return true;
}

// Adds the winding of a piece wholly left of the pixel over its span in the row.
fn add_left(i: u32, corner: vec2<f32>) -> bool {
    let edge = candidate(i);
    let extent = row_extent(edge, corner.y);
    if (edge.y == edge.w || extent.x > extent.y || extent.y > corner.x) { return true; }
    let top = max(min(edge.y, edge.w), corner.y);
    let bottom = min(max(edge.y, edge.w), corner.y + 1.0);
    if (!add_change(top, direction(edge))) { return false; }
    return bottom >= corner.y + 1.0 || add_change(bottom, -direction(edge));
}

// Coverage of an edge pixel from the near pieces and the left-side winding changes.
// Each 1/16-row slab (or the whole row when no two near pieces cross) is cut at those
// changes and at the ends of near pieces, so every piece present spans its interval.
// In an interval each near piece adds the exact area right of it times the change of
// the fill rule across it, with the windings on both sides taken at the middle.
fn interval_coverage(corner: vec2<f32>) -> f32 {
    var crossed = false;
    for (var j = 0u; j < near_count && !crossed; j++) {
        let a = candidate(near[j]);
        for (var k = j + 1u; k < near_count && !crossed; k++) {
            let b = candidate(near[k]);
            let upper = max(max(min(a.y, a.w), min(b.y, b.w)), corner.y);
            let lower = min(min(max(a.y, a.w), max(b.y, b.w)), corner.y + 1.0);
            if (lower <= upper) { continue; }
            let before = x_at(a, upper) - x_at(b, upper);
            let after = x_at(a, lower) - x_at(b, lower);
            crossed = (before < 0.0 && after > 0.0) || (before > 0.0 && after < 0.0);
        }
    }
    var coverage = 0.0;
    let count_slabs = select(1u, slabs, crossed);
    for (var s = 0u; s < count_slabs; s++) {
        let slab_bottom = corner.y + f32(s + 1u) / f32(count_slabs);
        var top = corner.y + f32(s) / f32(count_slabs);
        while (top < slab_bottom) {
            var bottom = slab_bottom;
            for (var j = 0u; j < change_count; j++) {
                let row = change_rows[j];
                if (row > top && row < bottom) { bottom = row; }
            }
            for (var j = 0u; j < near_count; j++) {
                let edge = candidate(near[j]);
                for (var end = 0u; end < 2u; end++) {
                    let y = select(edge.y, edge.w, end == 1u);
                    if (y > top && y < bottom) { bottom = y; }
                }
            }
            let middle = 0.5 * (top + bottom);
            var winding = 0;
            for (var j = 0u; j < change_count; j++) {
                if (change_rows[j] <= middle) { winding += changes[j]; }
            }
            coverage += inside(winding) * (bottom - top);
            for (var k = 0u; k < near_count; k++) {
                let edge = candidate(near[k]);
                let upper = max(min(edge.y, edge.w), top);
                let lower = min(max(edge.y, edge.w), bottom);
                if (lower <= upper) { continue; }
                let x = x_at(edge, middle);
                var before = winding;
                for (var j = 0u; j < near_count; j++) {
                    let other = candidate(near[j]);
                    if (j == k || !spans(other, middle)) { continue; }
                    let other_x = x_at(other, middle);
                    if (other_x < x || (other_x == x && j < k)) { before += direction(other); }
                }
                let area = right_area(edge, corner.x, upper, lower);
                coverage += (inside(before + direction(edge)) - inside(before)) * area;
            }
            top = bottom;
        }
    }
    return coverage;
}

// Edge pixel coverage scanning every piece of the row; negative when it overflows.
fn scanned_coverage(corner: vec2<f32>, count: u32) -> f32 {
    near_count = 0u;
    change_count = 0u;
    for (var i = 0u; i < count; i++) {
        if (!add_near(i, corner) || !add_left(i, corner)) { return -1.0; }
    }
    return interval_coverage(corner);
}

// Exact fill rules at 16x16 sample positions.
fn sampled_coverage(corner: vec2<f32>, count: u32) -> f32 {
    var total = 0.0;
    for (var j = 0u; j < 16u; j++) {
        let y = corner.y + (f32(j) + 0.5) / 16.0;
        // Reset explicitly: some compilers hoist loop-local variables.
        var windings: array<i32, 16>;
        for (var s = 0u; s < 16u; s++) { windings[s] = 0; }
        for (var i = 0u; i < count; i++) {
            let edge = candidate(i);
            if (!spans(edge, y)) { continue; }
            let x = x_at(edge, y);
            for (var s = 0u; s < 16u; s++) {
                if (x < corner.x + (f32(s) + 0.5) / 16.0) { windings[s] += direction(edge); }
            }
        }
        for (var s = 0u; s < 16u; s++) { total += inside(windings[s]); }
    }
    return total / 256.0;
}

// First piece whose top is at or below y; pieces are sorted by top.
fn first_edge_below(y: f32, edges: u32) -> u32 {
    var low = 0u;
    var high = edges;
    while (low < high) {
        let middle = (low + high) / 2u;
        let edge = edge_at(middle);
        if (min(edge.y, edge.w) < y) { low = middle + 1u; } else { high = middle; }
    }
    return low;
}

// Pixels of the run the piece passes through, as run positions (empty when first > last).
fn run_span(edge: vec4<f32>, top: f32, run_start: u32) -> vec2<i32> {
    let extent = row_extent(edge, top);
    if (extent.x > extent.y) { return vec2<i32>(1, 0); }
    let first = max(floor(extent.x), f32(run_start));
    let last = min(ceil(extent.y) - 1.0, f32(run_start + 255u));
    if (first > last) { return vec2<i32>(1, 0); }
    return vec2<i32>(i32(first) - i32(run_start), i32(last) - i32(run_start));
}

// Center winding at x from the sorted crossings left of x (or at x when inclusive).
fn sorted_winding(x: f32, crossings: u32, inclusive: bool) -> i32 {
    var low = 0u;
    var high = crossings;
    while (low < high) {
        let middle = (low + high) / 2u;
        if (sorted_x[middle] < x || (inclusive && sorted_x[middle] == x)) { low = middle + 1u; } else { high = middle; }
    }
    return select(bitcast<i32>(band_state.w), winding_before[min(low, band_capacity - 1u)], low < crossings);
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(workgroup_id) group: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>,
        @builtin(local_invocation_index) lane: u32) {
    let width = u32(params.offsets.x);
    let height = u32(params.offsets.y);
    let row = group.y * groups.x + group.x;
    if (row >= height) { return; }
    let top = f32(row);
    let center_y = top + 0.5;
    if (lane == 0u) {
        let edges = arrayLength(&data) / 4u;
        // Pieces never cross a row boundary, so the row's pieces start inside it.
        band_state = vec4<u32>(first_edge_below(top, edges), first_edge_below(top + 1.0, edges), 0u, 0u);
        atomicStore(&band_count, 0u);
        atomicStore(&crossing_count, 0u);
        atomicStore(&vertex_count, 0u);
    }
    let range = workgroupUniformLoad(&band_state).xy;
    for (var index = range.x + lane; index < range.y; index += 256u) {
        let edge = edge_at(index);
        if (max(edge.y, edge.w) > top && min(edge.y, edge.w) < top + 1.0) {
            let slot = atomicAdd(&band_count, 1u);
            if (slot < band_capacity) { band_edges[slot] = edge; }
        }
    }
    workgroupBarrier();
    if (lane == 0u) { band_state.z = atomicLoad(&band_count); }
    let loaded = workgroupUniformLoad(&band_state).z;
    global_mode = loaded > band_capacity;
    global_first = range.x;
    let count = select(loaded, range.y - range.x, global_mode);

    // Center crossings of the row, sorted with the winding left of each, and the pieces
    // with a vertex inside the row.
    for (var i = lane; i < count; i += 256u) {
        let edge = candidate(i);
        if (spans(edge, center_y)) {
            let slot = atomicAdd(&crossing_count, 1u);
            if (slot < band_capacity) {
                crossing_x[slot] = x_at(edge, center_y);
                crossing_direction[slot] = direction(edge);
            }
        }
        if (edge.y != edge.w && (min(edge.y, edge.w) > top || max(edge.y, edge.w) < top + 1.0)) {
            let slot = atomicAdd(&vertex_count, 1u);
            if (slot < vertex_capacity) { vertex_pieces[slot] = i; }
        }
    }
    workgroupBarrier();
    let crossings = atomicLoad(&crossing_count);
    let sorted = crossings <= band_capacity;
    if (sorted) {
        for (var i = lane; i < crossings; i += 256u) {
            var rank = 0u;
            for (var j = 0u; j < crossings; j++) {
                let before = crossing_x[j] < crossing_x[i] || (crossing_x[j] == crossing_x[i] && j < i);
                rank += select(0u, 1u, before);
            }
            sorted_x[rank] = crossing_x[i];
            winding_before[rank] = crossing_direction[i];
        }
    }
    workgroupBarrier();
    if (lane == 0u && sorted) {
        var sum = 0;
        for (var i = 0u; i < crossings; i++) {
            let direction_i = winding_before[i];
            winding_before[i] = sum;
            sum += direction_i;
        }
        band_state.w = bitcast<u32>(sum);
    }
    let vertices = atomicLoad(&vertex_count);

    // Runs of 256 pixels along the row.
    let runs = (width + 255u) / 256u;
    for (var run = 0u; run < runs; run++) {
        let run_start = run * 256u;
        let x = run_start + lane;
        atomicStore(&touched[lane], 0u);
        if (lane == 0u) { atomicStore(&edge_pixel_count, 0u); }
        workgroupBarrier();
        for (var i = lane; i < count; i += 256u) {
            let span = run_span(candidate(i), top, run_start);
            for (var p = span.x; p <= span.y; p++) { atomicAdd(&touched[p], 1u); }
        }
        workgroupBarrier();
        // Inclusive prefix sum of the counts (Hillis-Steele), then list offsets.
        list_start[lane] = atomicLoad(&touched[lane]);
        workgroupBarrier();
        for (var offset = 1u; offset < 256u; offset *= 2u) {
            let value = select(0u, list_start[lane - offset], lane >= offset);
            workgroupBarrier();
            list_start[lane] += value;
            workgroupBarrier();
        }
        let own = atomicLoad(&touched[lane]);
        let total = workgroupUniformLoad(&list_start[255]);
        workgroupBarrier();
        // Exclusive offsets; the counters become fill cursors ending at the next offset.
        list_start[lane] -= own;
        atomicStore(&touched[lane], list_start[lane]);
        let listed = total <= list_capacity && count <= 65536u;
        if (listed) {
            for (var i = lane; i < (total + 1u) / 2u; i += 256u) { atomicStore(&piece_list[i], 0u); }
        }
        workgroupBarrier();
        if (listed) {
            for (var i = lane; i < count; i += 256u) {
                let span = run_span(candidate(i), top, run_start);
                for (var p = span.x; p <= span.y; p++) {
                    let slot = atomicAdd(&touched[p], 1u);
                    atomicOr(&piece_list[slot / 2u], i << (16u * (slot % 2u)));
                }
            }
        }
        workgroupBarrier();

        let corner = vec2<f32>(f32(x), top);
        var winding = 0;
        if (x < width && !sorted) {
            for (var i = 0u; i < count; i++) {
                let edge = candidate(i);
                if (spans(edge, center_y) && x_at(edge, center_y) < corner.x + 0.5) { winding += direction(edge); }
            }
        } else if (x < width) {
            winding = sorted_winding(corner.x + 0.5, crossings, false);
        }
        pixel_bytes[lane] = quantize(inside(winding));
        // Edge pixels are gathered so the whole workgroup shares their heavier work.
        if (x < width && params.reserved.z == 1u && own > 0u) {
            edge_pixels[atomicAdd(&edge_pixel_count, 1u)] = lane;
        }
        workgroupBarrier();
        let edges_here = atomicLoad(&edge_pixel_count);
        for (var e = lane; e < edges_here; e += 256u) {
            let owner = edge_pixels[e];
            let position = vec2<f32>(f32(run_start + owner), top);
            var coverage = -1.0;
            if (listed && sorted && vertices <= vertex_capacity) {
                // Near pieces from the list; the left-side winding is the center winding
                // up to the pixel's left side without the near pieces, corrected along the
                // row by the vertex pieces wholly left of the pixel.
                near_count = 0u;
                change_count = 0u;
                var fits = true;
                for (var slot = list_start[owner]; fits && slot < atomicLoad(&touched[owner]); slot++) {
                    let piece = (atomicLoad(&piece_list[slot / 2u]) >> (16u * (slot % 2u))) & 65535u;
                    fits = add_near(piece, position);
                }
                // Pieces ending exactly on the pixel's left side count as left of it.
                var base = sorted_winding(position.x, crossings, true);
                for (var j = 0u; fits && j < near_count; j++) {
                    let edge = candidate(near[j]);
                    if (spans(edge, center_y) && x_at(edge, center_y) <= position.x) { base -= direction(edge); }
                }
                for (var j = 0u; fits && j < vertices; j++) {
                    let edge = candidate(vertex_pieces[j]);
                    let extent = row_extent(edge, top);
                    if (extent.x <= extent.y && extent.y <= position.x && spans(edge, center_y)) {
                        base -= direction(edge);
                    }
                }
                fits = fits && add_change(top, base);
                for (var j = 0u; fits && j < vertices; j++) {
                    fits = add_left(vertex_pieces[j], position);
                }
                if (fits) { coverage = interval_coverage(position); }
            }
            if (coverage < 0.0) { coverage = scanned_coverage(position, count); }
            if (coverage < 0.0) { coverage = sampled_coverage(position, count); }
            pixel_bytes[owner] = quantize(coverage);
        }
        workgroupBarrier();
        // Words wholly inside this run are written whole; the two that may be shared
        // with a neighboring run or row are merged byte by byte.
        let start = row * width + run_start;
        let end = row * width + min(run_start + 256u, width);
        let word = start / 4u + lane;
        if (lane < 65u && word * 4u < end) {
            var packed = 0u;
            var covered = 0u;
            for (var k = 0u; k < 4u; k++) {
                let pixel = word * 4u + k;
                if (pixel >= start && pixel < end) {
                    packed |= pixel_bytes[pixel - start] << (8u * k);
                    covered |= 255u << (8u * k);
                }
            }
            if (covered == 0xffffffffu) {
                let existing = atomicLoad(&mask_words[word]);
                let incoming = vec4<u32>(packed & 255u, (packed >> 8u) & 255u, (packed >> 16u) & 255u, packed >> 24u);
                atomicStore(&mask_words[word], combine_word(existing, incoming, params.reserved.x));
            } else {
                for (var k = 0u; k < 4u; k++) {
                    let shift = 8u * k;
                    if (((covered >> shift) & 255u) == 0u) { continue; }
                    let existing = (atomicLoad(&mask_words[word]) >> shift) & 255u;
                    let value = combine_coverage(existing, (packed >> shift) & 255u, params.reserved.x);
                    atomicAnd(&mask_words[word], ~(255u << shift));
                    atomicOr(&mask_words[word], value << shift);
                }
            }
        }
        workgroupBarrier();
    }
}
