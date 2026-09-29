//! include analysis
//! coverage kernel
//! entry none
// Partial moments per workgroup; params.reserved: unused, space, workgroups along x.
// Each invocation runs Welford updates over a grid-stride range; the workgroup merges them
// (Chan et al.) and writes one slot of 8 channels x (count, mean, m2 / scale^2, minimum,
// maximum):
// red, green, blue, alpha, luminosity, then premultiplied linear red, green, blue.
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> partials: array<f32>;

// m2 is stored divided by scale(minimum, maximum)^2, so finite HDR values up to the float
// limit never overflow the squared deviations. Scales are powers of two: exact rescaling.
struct Moments {
    count: f32,
    mean: f32,
    m2: f32,
    minimum: f32,
    maximum: f32,
};

const largest = 3.40282347e38;
var<workgroup> shared_moments: array<array<Moments, 8>, 64>;

// Largest power of two not above max(|minimum|, |maximum|, 1), capped at 2^126 so its
// reciprocal stays a normal float (GPUs flush subnormals); read back to undo m2's scale.
fn scale_exponent(minimum: f32, maximum: f32) -> u32 {
    let magnitude = max(max(abs(minimum), abs(maximum)), 1.0);
    return min(bitcast<u32>(magnitude) >> 23u, 253u);
}
fn power(exponent: u32) -> f32 {
    return bitcast<f32>(exponent << 23u);
}

// Welford update. Returns a value: naga miscompiles pointers to array elements here.
fn added(m: Moments, value: f32) -> Moments {
    if (m.count == 0.0) {
        return Moments(1.0, value, 0.0, value, value);
    }
    let minimum = min(m.minimum, value);
    let maximum = max(m.maximum, value);
    let e = scale_exponent(minimum, maximum);
    let inverse = power(254u - e);
    let ratio = power(127u + scale_exponent(m.minimum, m.maximum) - e);
    let count = m.count + 1.0;
    let delta = value * inverse - m.mean * inverse;
    let mean = m.mean + delta / count * power(e);
    return Moments(count, mean, m.m2 * ratio * ratio + delta * (value * inverse - mean * inverse),
                   minimum, maximum);
}

fn merge(a: Moments, b: Moments) -> Moments {
    if (b.count == 0.0) {
        return a;
    }
    if (a.count == 0.0) {
        return b;
    }
    let minimum = min(a.minimum, b.minimum);
    let maximum = max(a.maximum, b.maximum);
    let e = scale_exponent(minimum, maximum);
    let inverse = power(254u - e);
    let ra = power(127u + scale_exponent(a.minimum, a.maximum) - e);
    let rb = power(127u + scale_exponent(b.minimum, b.maximum) - e);
    let count = a.count + b.count;
    let delta = b.mean * inverse - a.mean * inverse;
    let share = b.count / count;
    return Moments(count, a.mean + delta * share * power(e),
                   a.m2 * ra * ra + b.m2 * rb * rb + delta * delta * a.count * share, minimum,
                   maximum);
}

@compute @workgroup_size(64, 1, 1)
fn main(@builtin(workgroup_id) group: vec3<u32>,
        @builtin(local_invocation_index) lane: u32) {
    var moments: array<Moments, 8>;
    for (var c = 0u; c < 8u; c += 1u) {
        moments[c] = Moments(0.0, 0.0, 0.0, largest, -largest);
    }
    let total = params.dispatch.z * params.dispatch.w;
    let stride = params.reserved.z * 64u;
    var i = group.x * 64u + lane;
    while (i < total) {
        let index = measured_index(i);
        if (operation_coverage(index) >= 0.5) {
            let pixel = source[index];
            var measured = measure(pixel);
            if (measured.color) {
                moments[0] = added(moments[0], measured.values[0]);
                moments[1] = added(moments[1], measured.values[1]);
                moments[2] = added(moments[2], measured.values[2]);
                moments[4] = added(moments[4], measured.values[4]);
            }
            moments[3] = added(moments[3], measured.values[3]);
            moments[5] = added(moments[5], pixel.r);
            moments[6] = added(moments[6], pixel.g);
            moments[7] = added(moments[7], pixel.b);
        }
        if (total - i <= stride) {
            break;
        }
        i += stride;
    }
    shared_moments[lane] = moments;
    workgroupBarrier();
    for (var width = 32u; width > 0u; width /= 2u) {
        if (lane < width) {
            for (var c = 0u; c < 8u; c += 1u) {
                shared_moments[lane][c] = merge(shared_moments[lane][c],
                                                shared_moments[lane + width][c]);
            }
        }
        workgroupBarrier();
    }
    if (lane < 8u) {
        let m = shared_moments[0][lane];
        let slot = group.x * 40u + lane * 5u;
        partials[slot] = m.count;
        partials[slot + 1u] = m.mean;
        partials[slot + 2u] = m.m2;
        partials[slot + 3u] = m.minimum;
        partials[slot + 4u] = m.maximum;
    }
}
