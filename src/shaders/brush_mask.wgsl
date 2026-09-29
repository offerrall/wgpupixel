// Sampled tip: the kernel's `tip` mask, params.dimensions.xy texels, its longer side
// scaled to the unit disk diameter (params.color2.y texels per unit). Each pixel
// averages up to 4x4 bilinear taps across its footprint.
fn tip_value(texel: vec2<i32>) -> f32 {
    if (any(texel < vec2<i32>(0)) || any(texel >= vec2<i32>(params.dimensions.xy))) {
        return 0.0;
    }
    let index = u32(texel.y) * params.dimensions.x + u32(texel.x);
    return f32((tip[index / 4u] >> ((index % 4u) * 8u)) & 255u) / 255.0;
}

fn tip_bilinear(position: vec2<f32>) -> f32 {
    let q = position - vec2<f32>(0.5);
    let origin = floor(q);
    let f = q - origin;
    let p = vec2<i32>(origin);
    return mix(mix(tip_value(p), tip_value(p + vec2<i32>(1, 0)), f.x),
               mix(tip_value(p + vec2<i32>(0, 1)), tip_value(p + vec2<i32>(1, 1)), f.x), f.y);
}

fn dab_coverage(offset: vec2<f32>, shape: mat2x2<f32>) -> f32 {
    let scale = params.color2.y;
    let center = vec2<f32>(params.dimensions.xy) * 0.5;
    let texels = scale * max(length(shape[0]), length(shape[1]));
    let reach = center + vec2<f32>(texels + 1.0);
    if (any(abs(scale * (shape * offset)) > reach)) { return 0.0; }
    let taps = u32(clamp(ceil(texels), 1.0, 4.0));
    var sum = 0.0;
    for (var j = 0u; j < taps; j += 1u) {
        for (var i = 0u; i < taps; i += 1u) {
            let sub = (vec2<f32>(f32(i), f32(j)) + vec2<f32>(0.5)) / f32(taps) - vec2<f32>(0.5);
            sum += tip_bilinear(scale * (shape * (offset + sub)) + center);
        }
    }
    return sum / f32(taps * taps);
}
