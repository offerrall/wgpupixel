fn blend_lum(c: vec3<f32>) -> f32 { return dot(c, vec3<f32>(0.3, 0.59, 0.11)); }
fn blend_sat(c: vec3<f32>) -> f32 { return max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b)); }
fn blend_set_lum(c: vec3<f32>, l: f32) -> vec3<f32> {
    var v = c + l - blend_lum(c);
    let n = min(v.r, min(v.g, v.b));
    let x = max(v.r, max(v.g, v.b));
    if (n < 0.0) { v = l + (v - l) * l / (l - n); }
    if (x > 1.0) { v = l + (v - l) * (1.0 - l) / (x - l); }
    return v;
}
fn blend_set_sat(c: vec3<f32>, s: f32) -> vec3<f32> {
    let n = min(c.r, min(c.g, c.b));
    let range = blend_sat(c);
    if (range == 0.0) { return vec3<f32>(0.0); }
    return (c - n) * (s / range);
}
fn blend_dodge(b: f32, source: f32) -> f32 {
    let s = clamp(source, 0.0, 1.0);
    if (b == 0.0) { return 0.0; }
    if (s == 1.0) { return max(1.0, b); }
    return clamp(b / (1.0 - s), b, max(1.0, b));
}
fn blend_burn(b: f32, source: f32) -> f32 {
    let s = clamp(source, 0.0, 1.0);
    if (b == 1.0) { return 1.0; }
    if (s == 0.0) { return min(0.0, b); }
    return clamp(1.0 - (1.0 - b) / s, min(0.0, b), b);
}
fn blend_vivid(b: f32, s: f32) -> f32 {
    if (s <= 0.5) { return blend_burn(b, 2.0 * s); }
    return blend_dodge(b, 2.0 * s - 1.0);
}
fn blend_channel(b: f32, s: f32, mode: u32) -> f32 {
    switch mode {
        case 11u: { return blend_dodge(b, s); }
        case 12u: { return blend_burn(b, s); }
        case 13u: { return max(min(0.0, b), b + s - 1.0); }
        case 14u: { return min(max(1.0, b), b + s); }
        case 15u: { return clamp(b + 2.0 * s - 1.0, min(0.0, b), max(1.0, b)); }
        case 16u: { return blend_vivid(b, s); }
        case 17u: { return select(max(b, 2.0 * s - 1.0), min(b, 2.0 * s), s <= 0.5); }
        case 18u: { return select(0.0, 1.0, b + s >= 1.0); }
        case 19u: { return max(min(0.0, b), b - s); }
        case 20u: {
            if (s <= 0.0) { return max(1.0, b); }
            return min(max(1.0, b), b / s);
        }
        default: { return s; }
    }
}
fn blend_nonseparable(b: vec3<f32>, s: vec3<f32>, mode: u32) -> vec3<f32> {
    switch mode {
        case 23u: { return blend_set_lum(blend_set_sat(s, blend_sat(b)), blend_lum(b)); }
        case 24u: { return blend_set_lum(blend_set_sat(b, blend_sat(s)), blend_lum(b)); }
        case 25u: { return blend_set_lum(s, blend_lum(b)); }
        default: { return blend_set_lum(b, blend_lum(s)); }
    }
}
fn blend_scale(c: vec3<f32>, exponent: i32) -> vec3<f32> {
    let parts = frexp(c);
    return ldexp(parts.fract, parts.exp - vec3<i32>(exponent));
}
fn blend_extended(b: vec3<f32>, s: vec3<f32>, mode: u32) -> vec3<f32> {
    if (mode >= 23u && mode <= 26u) {
        let low = min(vec3<f32>(0.0), min(b, s));
        let high = max(vec3<f32>(1.0), max(b, s));
        let lo = min(low.r, min(low.g, low.b));
        let hi = max(high.r, max(high.g, high.b));
        // Normalize mantissas rather than constructing a subnormal reciprocal.
        let exponent = frexp(max(-lo, hi)).exp;
        let origin = blend_scale(vec3<f32>(lo), exponent).x;
        let width = blend_scale(vec3<f32>(hi), exponent).x - origin;
        let result = blend_nonseparable((blend_scale(b, exponent) - origin) / width,
                                       (blend_scale(s, exponent) - origin) / width, mode);
        return ldexp(result * width + origin, vec3<i32>(exponent));
    }
    if (mode == 21u || mode == 22u) {
        let magnitude = max(abs(b), abs(s));
        let exponent = frexp(max(1.0, max(magnitude.r, max(magnitude.g, magnitude.b)))).exp;
        let source_sum = dot(blend_scale(s, exponent), vec3<f32>(1.0));
        let backdrop_sum = dot(blend_scale(b, exponent), vec3<f32>(1.0));
        let use_source = select((source_sum < backdrop_sum), (source_sum > backdrop_sum), mode == 22u);
        return select(b, s, use_source);
    }
    return vec3<f32>(blend_channel(b.r, s.r, mode), blend_channel(b.g, s.g, mode), blend_channel(b.b, s.b, mode));
}
fn blend_random(x: u32, y: u32, seed: u32) -> f32 {
    var h = seed ^ (x * 0x9e3779b9u) ^ (y * 0x85ebca6bu);
    h = (h ^ (h >> 16u)) * 0x7feb352du;
    h = (h ^ (h >> 15u)) * 0x846ca68bu;
    h = h ^ (h >> 16u);
    return f32(h >> 8u) / 16777216.0;
}
fn blend_sample(original: vec4<f32>, backdrop: vec4<f32>, opacity: f32, mode: u32, coordinate: vec2<u32>, seed: u32) -> vec4<f32> {
    if (mode == 27u) {
        if (original.a * opacity <= blend_random(coordinate.x, coordinate.y, seed)) { return backdrop; }
        return vec4<f32>(original.rgb / original.a, 1.0);
    }
    return blend_pixel(original, backdrop, opacity, mode);
}
