// Computed elliptical tip. shape maps a pixel offset into the unit disk; the rim
// distance follows from the gradient of |shape * offset|. Hardness (params.color2.x)
// sets the smoothstep falloff width, at least `ramp` pixels.
fn round_profile(offset: vec2<f32>, shape: mat2x2<f32>, ramp: f32) -> f32 {
    let u = shape * offset;
    let radius = length(u);
    if (radius <= 1e-6) { return 1.0; }
    let gradient = length(transpose(shape) * u) / radius;
    let edge = (1.0 - radius) / gradient;
    let softness = max(ramp, (1.0 - params.color2.x) / gradient);
    let t = clamp((edge + 0.5 * ramp) / softness, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

fn dab_coverage(offset: vec2<f32>, shape: mat2x2<f32>) -> f32 {
    // Semi-axes are the reciprocal lengths of the shape's rows.
    let major = 1.0 / length(vec2<f32>(shape[0].x, shape[1].x));
    let minor = 1.0 / length(vec2<f32>(shape[0].y, shape[1].y));
    if (minor >= 2.0 && minor * minor >= major) {
        return round_profile(offset, shape, 1.0);
    }
    // The rim distance estimate fails where the rim bends within a pixel (small tips,
    // thin tips' ends): average 4x4 subsamples with a quarter-pixel rim instead.
    var sum = 0.0;
    for (var j = 0u; j < 4u; j += 1u) {
        for (var i = 0u; i < 4u; i += 1u) {
            let sub = (vec2<f32>(f32(i), f32(j)) + vec2<f32>(0.5)) * 0.25 - vec2<f32>(0.5);
            sum += round_profile(offset + sub, shape, 0.25);
        }
    }
    return sum * (1.0 / 16.0);
}
