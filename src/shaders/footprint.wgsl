// Inverse-mapped resampling shared by the geometry kernels. The including kernel
// defines footprint_fetch(pixel) for pixels of the sampled level (see
// footprint_level). Coordinates are continuous: source pixel (i, j) covers
// [i, i + 1) x [j, j + 1). Methods use the ResizeFilter values and edges the
// EdgeMode values.
//
// The destination pixel's footprint in the source is the parallelogram spanned by
// the Jacobian columns. bilinear, bicubic and lanczos stretch along its principal
// axes wherever it is wider than a source pixel, so minification averages instead
// of aliasing. area weighs each source pixel by its exact overlap with the
// parallelogram. Callers bound the work per pixel with `limit` (the largest
// footprint scale evaluated directly) and keep footprints within it, so no source
// content is skipped: resize splits into one pass per axis, affine transforms first
// average the source, and projective ones sample a box pyramid (footprint_map).
const footprint_taps = 64.0;
const footprint_taps_extended = 24.0;

// Pyramid level read by footprint_fetch: 0 is the source; level k > 0 starts at
// pixel footprint_offset of the pyramid and is footprint_width pixels wide.
var<private> footprint_level: u32 = 0u;
var<private> footprint_offset: u32 = 0u;
var<private> footprint_width: u32 = 0u;

fn footprint_radius(method: u32) -> f32 {
    return array<f32, 5>(0.5, 1.0, 2.0, 3.0, 0.5)[method];
}

// Largest footprint scale sampled directly on one level. With transparent edges only
// destination pixels near the source do this work; with clamp, repeat or mirror edges
// every destination pixel can, so the budget is smaller (area, whose boundary pixels
// need exact clipping, the smallest).
fn footprint_limit(method: u32, edge: u32) -> f32 {
    if (edge != 0u && method == 4u) { return 6.0; }
    let taps = select(footprint_taps_extended, footprint_taps, edge == 0u);
    return 0.5 * taps / footprint_radius(method);
}

fn footprint_kernel(t: f32, method: u32) -> f32 {
    let x = abs(t);
    if (method == 2u) {
        if (x <= 1.0) { return ((1.5 * x - 2.5) * x) * x + 1.0; }
        if (x < 2.0) { return ((-0.5 * x + 2.5) * x - 4.0) * x + 2.0; }
        return 0.0;
    }
    if (method == 3u) {
        if (x >= 3.0) { return 0.0; }
        if (x < 0.00001) { return 1.0; }
        let p = 3.141592653589793 * x;
        return 3.0 * sin(p) * sin(p / 3.0) / (p * p);
    }
    return max(1.0 - x, 0.0);
}

// Length of [d - 0.5, d + 0.5] (a source pixel) inside [-width / 2, width / 2].
fn footprint_overlap(d: f32, width: f32) -> f32 {
    return max(min(d + 0.5, 0.5 * width) - max(d - 0.5, -0.5 * width), 0.0);
}

// Integral over x in [low.x, high.x] of the height of the edge a -> b above low.y,
// clamped to the box [low, high]; signed by the edge's x direction. Summed over a
// closed polygon this is the (signed) area of the polygon inside the box.
fn footprint_edge_area(a: vec2<f32>, b: vec2<f32>, low: vec2<f32>, high: vec2<f32>) -> f32 {
    if (a.x == b.x) { return 0.0; }
    let left = select(b, a, a.x < b.x);
    let right = select(a, b, a.x < b.x);
    let x0 = max(left.x, low.x);
    let x1 = min(right.x, high.x);
    if (!(x0 < x1)) { return 0.0; }
    let slope = (right.y - left.y) / (right.x - left.x);
    // Split where the edge crosses the box's bottom and top, so each piece is linear.
    var cut0 = x1;
    var cut1 = x1;
    if (slope != 0.0) {
        let c0 = clamp(left.x + (low.y - left.y) / slope, x0, x1);
        let c1 = clamp(left.x + (high.y - left.y) / slope, x0, x1);
        cut0 = min(c0, c1);
        cut1 = max(c0, c1);
    }
    var sum = 0.0;
    var start = x0;
    for (var piece = 0u; piece < 3u; piece++) {
        let end = select(select(x1, cut1, piece == 1u), cut0, piece == 0u);
        if (end > start) {
            let y0 = clamp(left.y + slope * (start - left.x), low.y, high.y) - low.y;
            let y1 = clamp(left.y + slope * (end - left.x), low.y, high.y) - low.y;
            sum += 0.5 * (end - start) * (y0 + y1);
        }
        start = max(start, end);
    }
    return select(-sum, sum, a.x < b.x);
}

// Area of the parallelogram centered at the origin with edge vectors c0 and c1,
// intersected with the box [low, high].
fn footprint_coverage(c0: vec2<f32>, c1: vec2<f32>, low: vec2<f32>, high: vec2<f32>) -> f32 {
    let v0 = -0.5 * c0 - 0.5 * c1;
    let v1 = 0.5 * c0 - 0.5 * c1;
    let v2 = 0.5 * c0 + 0.5 * c1;
    let v3 = -0.5 * c0 + 0.5 * c1;
    return abs(footprint_edge_area(v0, v1, low, high) + footprint_edge_area(v1, v2, low, high) +
               footprint_edge_area(v2, v3, low, high) + footprint_edge_area(v3, v0, low, high));
}

// Classifies the source pixel centered at offset d against the footprint: 1 when
// entirely inside, 0 when separated from it along a footprint axis, 2 otherwise.
// `unit` maps offsets to footprint coordinates, where the footprint is [-0.5, 0.5]^2.
fn footprint_classify(unit: mat2x2<f32>, d: vec2<f32>) -> u32 {
    let a = unit * (d + vec2<f32>(-0.5, -0.5));
    let b = unit * (d + vec2<f32>(0.5, -0.5));
    let c = unit * (d + vec2<f32>(0.5, 0.5));
    let e = unit * (d + vec2<f32>(-0.5, 0.5));
    let low = min(min(a, b), min(c, e));
    let high = max(max(a, b), max(c, e));
    if (any(low >= vec2<f32>(0.5)) || any(high <= vec2<f32>(-0.5))) { return 0u; }
    return select(2u, 1u, all(low >= vec2<f32>(-0.5)) && all(high <= vec2<f32>(0.5)));
}

// Applies the edge mode to one integer coordinate; -1 marks a transparent pixel.
// mirror reflects about the edges and repeats the edge pixel (period 2 * extent).
fn footprint_axis(coordinate: i32, extent: i32, edge: u32) -> i32 {
    if (edge == 1u) { return clamp(coordinate, 0, extent - 1); }
    if (edge == 2u) {
        let m = coordinate % extent;
        return select(m, m + extent, m < 0);
    }
    if (edge == 3u) {
        let period = 2 * extent;
        var m = coordinate % period;
        if (m < 0) { m += period; }
        return select(m, period - 1 - m, m >= extent);
    }
    return select(coordinate, -1, coordinate < 0 || coordinate >= extent);
}

fn footprint_pixel(pixel: vec2<i32>, size: vec2<i32>, edge: u32) -> vec4<f32> {
    let x = footprint_axis(pixel.x, size.x, edge);
    let y = footprint_axis(pixel.y, size.y, edge);
    if (x < 0 || y < 0) { return vec4<f32>(0.0); }
    return footprint_fetch(vec2<i32>(x, y));
}

// Footprint scale along a principal axis with squared length `value`: never below
// one source pixel, never beyond `limit`.
fn footprint_scale(value: f32, limit: f32) -> f32 {
    return clamp(sqrt(max(value, 0.0)), 1.0, max(limit, 1.0));
}

// Moves a sample position by whole periods (or pixels for clamp) so the tap
// coordinates stay small. Remote clamped positions, where float spacing exceeds
// a pixel, are simply clamped: every tap then reads the same edge pixels.
fn footprint_reduce(q: vec2<f32>, extent: vec2<f32>, reach: vec2<f32>, edge: u32) -> vec2<f32> {
    if (edge == 1u) {
        let low = -reach - 1.0;
        let high = extent + reach + 1.0;
        let near = q - max(floor(q - high), vec2<f32>(0.0)) + max(floor(low - q), vec2<f32>(0.0));
        let remote = (q > high + 4096.0) | (q < low - 4096.0);
        return select(near, clamp(q, low, high), remote);
    }
    let period = extent * select(1.0, 2.0, edge == 3u);
    return clamp(q - period * floor(q / period), vec2<f32>(0.0), period);
}

// Squared principal lengths of the footprint: the eigenvalues of J J^T, the smaller
// one from the determinant to avoid cancellation.
fn footprint_axes(c0: vec2<f32>, c1: vec2<f32>) -> vec2<f32> {
    let p = c0.x * c0.x + c1.x * c1.x;
    let r = c0.y * c0.y + c1.y * c1.y;
    let k = c0.x * c0.y + c1.x * c1.y;
    let determinant = c0.x * c1.y - c1.x * c0.y;
    let large = 0.5 * (p + r) + sqrt(0.25 * (p - r) * (p - r) + k * k);
    return vec2<f32>(large, select(determinant * determinant / large, 0.0, large <= 0.0));
}

// Samples the current level at continuous position `center`. The Jacobian columns
// are the displacements of one destination pixel step in x and in y.
fn footprint_sample(center: vec2<f32>, jacobian: mat2x2<f32>, size: vec2<i32>, method: u32,
                    edge: u32, limit: f32) -> vec4<f32> {
    // Rejects NaN and infinite positions as well as remote ones.
    if (!all(abs(center) < vec2<f32>(1e30))) { return vec4<f32>(0.0); }
    let extent = vec2<f32>(size);
    if (method == 0u) {
        var q = center;
        if (edge == 0u) {
            if (any(q < vec2<f32>(0.0)) || any(q >= extent)) { return vec4<f32>(0.0); }
        } else {
            q = footprint_reduce(q, extent, vec2<f32>(0.0), edge);
        }
        return footprint_pixel(vec2<i32>(floor(q)), size, edge);
    }
    let c0 = jacobian[0];
    let c1 = jacobian[1];
    let area = method == 4u;
    let determinant = c0.x * c1.y - c1.x * c0.y;
    var s = mat2x2<f32>(1.0, 0.0, 0.0, 1.0);
    var reach: vec2<f32>;
    var separable = c1.x == 0.0 && c0.y == 0.0;
    // With transparent edges, outside pixels contribute only weight. When that weight
    // has a closed form, taps are limited to the source and the full weight is exact
    // (area: the parallelogram's area) or the kernel integral (footprints beyond the
    // limit, which only occur once the source cannot shrink further).
    var analytic = 0.0;
    if (area) {
        // The parallelogram itself, widened by half a source pixel.
        reach = 0.5 * vec2<f32>(abs(c0.x) + abs(c1.x), abs(c0.y) + abs(c1.y)) + 0.5;
        if (edge == 0u) {
            analytic = abs(determinant);
        } else {
            reach = min(reach, vec2<f32>(min(limit + 1.0, 1e6)));
        }
    } else {
        // F = f(J J^T) as a symmetric matrix; kernels are evaluated on S d, S = F^-1.
        let axes = footprint_axes(c0, c1);
        var bound = limit;
        if (edge == 0u && axes.x > limit * limit) {
            bound = 1e30;
        }
        let p = c0.x * c0.x + c1.x * c1.x;
        let r = c0.y * c0.y + c1.y * c1.y;
        let k = c0.x * c0.y + c1.x * c1.y;
        let f_small = footprint_scale(axes.y, bound);
        var f = mat2x2<f32>(f_small, 0.0, 0.0, f_small);
        if (axes.x - axes.y > 0.000001 * axes.x) {
            let slope = (footprint_scale(axes.x, bound) - f_small) / (axes.x - axes.y);
            f = mat2x2<f32>(slope * (p - axes.y) + f_small, slope * k, slope * k,
                            slope * (r - axes.y) + f_small);
        }
        let f_determinant = f[0][0] * f[1][1] - f[1][0] * f[0][1];
        s = mat2x2<f32>(f[1][1], -f[0][1], -f[1][0], f[0][0]) * (1.0 / f_determinant);
        separable = s[1][0] == 0.0 && s[0][1] == 0.0;
        let support = footprint_radius(method);
        reach = support * vec2<f32>(abs(f[0][0]) + abs(f[1][0]), abs(f[0][1]) + abs(f[1][1]));
        if (bound > limit) {
            let integral = select(1.0, 0.99705534595, method == 3u);
            analytic = f_determinant * integral * integral;
        }
    }
    let unit = mat2x2<f32>(c1.y, -c0.y, -c1.x, c0.x) * (1.0 / determinant);
    var q = center;
    if (edge == 0u) {
        if (any(q + reach <= vec2<f32>(0.0)) || any(q - reach >= extent)) {
            return vec4<f32>(0.0);
        }
    } else {
        q = footprint_reduce(q, extent, reach, edge);
    }
    var first = vec2<i32>(ceil(q - reach - 0.5));
    var last = vec2<i32>(floor(q + reach - 0.5));
    if (analytic > 0.0) {
        first = max(first, vec2<i32>(0));
        last = min(last, size - 1);
    }
    var sum = vec4<f32>(0.0);
    var total = 0.0;
    for (var y = first.y; y <= last.y; y++) {
        let dy = f32(y) + 0.5 - q.y;
        let row = s[1] * dy;
        var row_weight = 1.0;
        if (separable) {
            row_weight = select(footprint_kernel(row.y, method), footprint_overlap(dy, abs(c1.y)),
                                area);
            if (row_weight == 0.0) { continue; }
        }
        for (var x = first.x; x <= last.x; x++) {
            let dx = f32(x) + 0.5 - q.x;
            var weight = row_weight;
            if (area) {
                if (separable) {
                    weight *= footprint_overlap(dx, abs(c0.x));
                } else {
                    let d = vec2<f32>(dx, dy);
                    let kind = footprint_classify(unit, d);
                    weight = select(f32(kind), footprint_coverage(c0, c1, d - 0.5, d + 0.5),
                                    kind == 2u);
                }
            } else {
                let t = s[0] * dx + row;
                weight *= footprint_kernel(t.x, method);
                if (!separable) { weight *= footprint_kernel(t.y, method); }
            }
            if (weight != 0.0) {
                sum += footprint_pixel(vec2<i32>(x, y), size, edge) * weight;
                total += weight;
            }
        }
    }
    if (analytic > 0.0) { total = analytic; }
    if (!(abs(total) > 0.0)) { return vec4<f32>(0.0); }
    return sum / total;
}

// Exact area average over a large affine footprint, via Green's theorem: the integral
// of f over the parallelogram is the loop integral of Q dy, where Q(x) integrates f
// along the row from x_ref. The kernel defines footprint_table(index), the per-row
// table built by area_table: for row r (stride w + ceil(w / 4096)), inclusive prefix
// sums within each 64-pixel chunk, then one total per 4096-pixel group. Sums read
// only small partial values, so precision does not depend on the row position.
const area_chunk = 64u;

fn area_stride(w: u32) -> u32 {
    return w + (w + 4095u) / 4096u;
}

fn area_prefix(r: u32, i: u32, w: u32) -> vec4<f32> {
    return footprint_table(r * area_stride(w) + i);
}

fn area_chunk_total(r: u32, c: u32, w: u32) -> vec4<f32> {
    return area_prefix(r, min(c * area_chunk + area_chunk - 1u, w - 1u), w);
}

// Sum of row r over pixels [a, b), 0 <= a <= b <= w.
fn area_sum(r: u32, a: u32, b: u32, w: u32) -> vec4<f32> {
    if (a >= b) { return vec4<f32>(0.0); }
    let first = a / area_chunk;
    let last = (b - 1u) / area_chunk;
    let before = select(vec4<f32>(0.0), area_prefix(r, a - 1u, w), a % area_chunk != 0u);
    if (first == last) { return area_prefix(r, b - 1u, w) - before; }
    var sum = area_chunk_total(r, first, w) - before + area_prefix(r, b - 1u, w);
    var c = first + 1u;
    while (c < last && c % 64u != 0u) {
        sum += area_chunk_total(r, c, w);
        c++;
    }
    while (c + 64u <= last) {
        sum += footprint_table(r * area_stride(w) + w + c / 64u);
        c += 64u;
    }
    while (c < last) {
        sum += area_chunk_total(r, c, w);
        c++;
    }
    return sum;
}

fn area_floor_div(a: i32, b: i32) -> i32 {
    let q = a / b;
    return select(q, q - 1, a % b != 0 && (a < 0) != (b < 0));
}

// Mirrored period [0, 2w): sum over [a, b) with 0 <= a <= b <= 2w.
fn area_mirror_sum(r: u32, a: u32, b: u32, w: u32) -> vec4<f32> {
    return area_sum(r, min(a, w), min(b, w), w) +
           area_sum(r, 2u * w - max(b, w), 2u * w - max(a, w), w);
}

// Sum over integer pixels [a, b) of row r extended by the edge mode, a <= b.
fn area_extended(r: u32, a: i32, b: i32, w: i32, edge: u32) -> vec4<f32> {
    let uw = u32(w);
    if (edge == 0u) { return area_sum(r, u32(clamp(a, 0, w)), u32(clamp(b, 0, w)), uw); }
    if (edge == 1u) {
        var sum = area_sum(r, u32(clamp(a, 0, w)), u32(clamp(b, 0, w)), uw);
        if (a < 0) {
            sum += footprint_fetch(vec2<i32>(0, i32(r))) * f32(min(b, 0) - a);
        }
        if (b > w) {
            sum += footprint_fetch(vec2<i32>(w - 1, i32(r))) * f32(b - max(a, w));
        }
        return sum;
    }
    let period = select(w, 2 * w, edge == 3u);
    let ka = area_floor_div(a, period);
    let kb = area_floor_div(b, period);
    let ra = u32(a - ka * period);
    let rb = u32(b - kb * period);
    if (edge == 2u) {
        if (ka == kb) { return area_sum(r, ra, rb, uw); }
        return area_sum(r, ra, uw, uw) + area_sum(r, 0u, uw, uw) * f32(kb - ka - 1) +
               area_sum(r, 0u, rb, uw);
    }
    if (ka == kb) { return area_mirror_sum(r, ra, rb, uw); }
    return area_mirror_sum(r, ra, 2u * uw, uw) + area_sum(r, 0u, uw, uw) * (2.0 * f32(kb - ka - 1)) +
           area_mirror_sum(r, 0u, rb, uw);
}

fn area_pixel(r: u32, i: i32, w: i32, edge: u32) -> vec4<f32> {
    let x = footprint_axis(i, w, edge);
    if (x < 0) { return vec4<f32>(0.0); }
    return footprint_fetch(vec2<i32>(x, i32(r)));
}

// Q(x): integral of row r from x_ref (an integer) to x.
fn area_q(r: u32, x: f32, x_ref: i32, w: i32, edge: u32) -> vec4<f32> {
    let i = i32(floor(x));
    var whole: vec4<f32>;
    if (i >= x_ref) {
        whole = area_extended(r, x_ref, i, w, edge);
    } else {
        whole = -area_extended(r, i, x_ref, w, edge);
    }
    return whole + area_pixel(r, i, w, edge) * (x - f32(i));
}

// Mean of Q over [lo, hi]: Q(lo) + (1 / (hi - lo)) * integral of f(t) (hi - t) dt.
fn area_mean_q(r: u32, lo: f32, hi: f32, x_ref: i32, w: i32, edge: u32) -> vec4<f32> {
    let base = area_q(r, lo, x_ref, w, edge);
    let length = hi - lo;
    if (!(length > 0.000001)) { return base; }
    var moment = vec4<f32>(0.0);
    var start = i32(floor(lo));
    var end = i32(ceil(hi));
    if (edge <= 1u) {
        // Outside the row, f is zero (transparent) or the edge pixel (clamp).
        if (edge == 1u) {
            let left = min(hi, 0.0);
            if (lo < left) {
                moment += footprint_fetch(vec2<i32>(0, i32(r))) *
                          ((left - lo) * (hi - 0.5 * (lo + left)));
            }
            let right = max(lo, f32(w));
            if (right < hi) {
                moment += footprint_fetch(vec2<i32>(w - 1, i32(r))) *
                          ((hi - right) * (hi - 0.5 * (right + hi)));
            }
        }
        start = max(start, 0);
        end = min(end, w);
    }
    for (var i = start; i < end; i++) {
        let a = max(lo, f32(i));
        let b = min(hi, f32(i + 1));
        if (b > a) {
            moment += area_pixel(r, i, w, edge) * ((b - a) * (hi - 0.5 * (a + b)));
        }
    }
    return base + moment / length;
}

// Loop integral of Q dy along the edge from a to b.
fn area_edge(a: vec2<f32>, b: vec2<f32>, x_ref: i32, size: vec2<i32>, edge: u32) -> vec4<f32> {
    if (a.y == b.y) { return vec4<f32>(0.0); }
    let direction = sign(b.y - a.y);
    let top = select(b, a, a.y < b.y);
    let bottom = select(a, b, a.y < b.y);
    let slope = (bottom.x - top.x) / (bottom.y - top.y);
    var y0 = top.y;
    var y1 = bottom.y;
    let height = f32(size.y);
    var sum = vec4<f32>(0.0);
    if (edge == 0u) {
        y0 = max(y0, 0.0);
        y1 = min(y1, height);
    } else if (edge == 1u) {
        // Rows beyond the source repeat its edge rows: one piece per side.
        if (y0 < 0.0) {
            let end = min(y1, 0.0);
            let xa = top.x + slope * (y0 - top.y);
            let xb = top.x + slope * (end - top.y);
            sum += area_mean_q(0u, min(xa, xb), max(xa, xb), x_ref, size.x, edge) * (end - y0);
            y0 = end;
        }
        if (y1 > height) {
            let start = max(y0, height);
            let xa = top.x + slope * (start - top.y);
            let xb = top.x + slope * (y1 - top.y);
            sum += area_mean_q(u32(size.y - 1), min(xa, xb), max(xa, xb), x_ref, size.x, edge) *
                   (y1 - start);
            y1 = start;
        }
    }
    var y = y0;
    while (y < y1) {
        let row = floor(y);
        let next = min(row + 1.0, y1);
        let r = footprint_axis(i32(row), size.y, edge);
        if (r >= 0) {
            let xa = top.x + slope * (y - top.y);
            let xb = top.x + slope * (next - top.y);
            sum += area_mean_q(u32(r), min(xa, xb), max(xa, xb), x_ref, size.x, edge) * (next - y);
        }
        y = next;
    }
    return sum * direction;
}

// Exact mean of the edge-extended source over the parallelogram q + J [-1/2, 1/2]^2.
fn area_integral(center: vec2<f32>, c0: vec2<f32>, c1: vec2<f32>, size: vec2<i32>,
                 edge: u32) -> vec4<f32> {
    if (!all(abs(center) < vec2<f32>(1e30))) { return vec4<f32>(0.0); }
    let extent = vec2<f32>(size);
    let reach = 0.5 * vec2<f32>(abs(c0.x) + abs(c1.x), abs(c0.y) + abs(c1.y));
    var q = center;
    if (edge == 0u) {
        if (any(q + reach <= vec2<f32>(0.0)) || any(q - reach >= extent)) {
            return vec4<f32>(0.0);
        }
    } else {
        q = footprint_reduce(q, extent, reach, edge);
    }
    let v0 = q - 0.5 * c0 - 0.5 * c1;
    let v1 = q + 0.5 * c0 - 0.5 * c1;
    let v2 = q + 0.5 * c0 + 0.5 * c1;
    let v3 = q - 0.5 * c0 + 0.5 * c1;
    let x_ref = i32(floor(q.x));
    let sum = area_edge(v0, v1, x_ref, size, edge) + area_edge(v1, v2, x_ref, size, edge) +
              area_edge(v2, v3, x_ref, size, edge) + area_edge(v3, v0, x_ref, size, edge);
    // The same loop integral of x dy is the signed area, det J.
    return sum / (c0.x * c1.y - c1.x * c0.y);
}

// Maps a destination position through the projective matrix whose rows are
// params.values, params.color1 and params.color2 (destination to source), with
// params.reserved.x the method and params.reserved.y the edge mode. Positions with
// a nonpositive homogeneous weight lie beyond the horizon and are empty.
// params.reserved.w counts the box pyramid levels available beyond the source:
// level k halves level k - 1 (rounding sizes up), so its pixel i covers source
// [i * 2^k, (i + 1) * 2^k). Footprints wider than footprint_limit are sampled on
// the finest level where they fit.
fn footprint_map(position: vec2<f32>) -> vec4<f32> {
    let h = vec3<f32>(position, 1.0);
    let w = dot(params.color2.xyz, h);
    if (!(w > 0.0)) { return vec4<f32>(0.0); }
    var q = vec2<f32>(dot(params.values.xyz, h), dot(params.color1.xyz, h)) / w;
    var c0 = vec2<f32>(params.values.x - q.x * params.color2.x,
                       params.color1.x - q.y * params.color2.x) / w;
    var c1 = vec2<f32>(params.values.y - q.x * params.color2.y,
                       params.color1.y - q.y * params.color2.y) / w;
    let method = params.reserved.x;
    // params.values.w > 0 overrides the direct-sampling limit (resize passes).
    let limit = select(footprint_limit(method, params.reserved.y), params.values.w,
                       params.values.w > 0.0);
    var size = vec2<u32>(params.dimensions.xy);
    footprint_level = 0u;
    footprint_offset = 0u;
    // params.reserved.w == 0xffffffff: area over an affine footprint integrated with the
    // area table at binding 4 instead of pyramid levels.
    if (params.reserved.w == 0xffffffffu) {
        return area_integral(q, c0, c1, vec2<i32>(size), params.reserved.y);
    }
    if (method != 0u && params.reserved.w > 0u) {
        let largest = sqrt(footprint_axes(c0, c1).x);
        if (largest > limit) {
            let wanted = u32(ceil(log2(largest / limit)));
            let level = min(wanted, params.reserved.w);
            for (var k = 1u; k <= level; k++) {
                if (k > 1u) { footprint_offset += size.x * size.y; }
                size = (size + vec2<u32>(1u)) / 2u;
            }
            footprint_level = level;
            footprint_width = size.x;
            let scale = 1.0 / f32(1u << level);
            q *= scale;
            c0 *= scale;
            c1 *= scale;
        }
    }
    return footprint_sample(q, mat2x2<f32>(c0, c1), vec2<i32>(size), method,
                            params.reserved.y, limit);
}
