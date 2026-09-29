fn adjustment_encode(v: vec3<f32>) -> vec3<f32> {
    let c = clamp(v, vec3<f32>(0.0), vec3<f32>(1.0));
    let encoded = select(1.055 * pow(c, vec3<f32>(1.0 / 2.4)) - 0.055, 12.92 * c,
                         c <= vec3<f32>(0.0031308));
    return select(encoded, vec3<f32>(1.0), c == vec3<f32>(1.0));
}
fn adjustment_decode(v: vec3<f32>) -> vec3<f32> {
    let c = clamp(v, vec3<f32>(0.0), vec3<f32>(1.0));
    return select(pow((c + 0.055) / 1.055, vec3<f32>(2.4)), c / 12.92,
                  c <= vec3<f32>(0.04045));
}
fn adjustment_luma(c: vec3<f32>) -> f32 {
    return dot(c, vec3<f32>(0.2126, 0.7152, 0.0722));
}
fn adjustment_preserve_extended(c: vec3<f32>, source: vec3<f32>) -> vec3<f32> {
    let luminance = adjustment_luma(source);
    let shifted = c + vec3<f32>(luminance - adjustment_luma(c));
    let low = min(0.0, min(source.r, min(source.g, source.b)));
    let high = max(1.0, max(source.r, max(source.g, source.b)));
    let minimum = min(shifted.r, min(shifted.g, shifted.b));
    let maximum = max(shifted.r, max(shifted.g, shifted.b));
    var scale = 1.0;
    if (minimum < low) { scale = min(scale, (luminance-low) / (luminance-minimum)); }
    if (maximum > high) { scale = min(scale, (high-luminance) / (maximum-luminance)); }
    return clamp(vec3<f32>(luminance) + (shifted-vec3<f32>(luminance)) * scale,
                 vec3<f32>(low), vec3<f32>(high));
}
fn adjustment_hsl(c: vec3<f32>) -> vec3<f32> {
    let low = min(c.r, min(c.g, c.b));
    let high = max(c.r, max(c.g, c.b));
    let delta = high - low;
    let lightness = (high + low) * 0.5;
    if (delta == 0.0) { return vec3<f32>(0.0, 0.0, lightness); }
    var h = (c.g - c.b) / delta;
    if (high != c.r) {
        h = select((c.r - c.g) / delta + 4.0, (c.b - c.r) / delta + 2.0, high == c.g);
    }
    return vec3<f32>(fract(h / 6.0 + 1.0), delta / max(delta, 1.0 - abs(2.0 * lightness - 1.0)), lightness);
}
fn adjustment_rgb(hsl: vec3<f32>) -> vec3<f32> {
    let h = fract(hsl.x + 1.0);
    let c = (1.0 - abs(2.0 * hsl.z - 1.0)) * hsl.y;
    let shape = clamp(abs(fract(vec3<f32>(h) + vec3<f32>(0.0, 2.0/3.0, 1.0/3.0)) * 6.0 - 3.0) - 1.0,
                      vec3<f32>(0.0), vec3<f32>(1.0));
    return (shape - 0.5) * c + hsl.z;
}

fn adjustment_encode_extended(v: vec3<f32>) -> vec3<f32> {
    let c = abs(v);
    return sign(v) * select(1.055 * pow(c, vec3<f32>(1.0 / 2.4)) - 0.055, 12.92 * min(c, vec3<f32>(0.0031308)),
                            c <= vec3<f32>(0.0031308));
}
fn adjustment_decode_extended(v: vec3<f32>) -> vec3<f32> {
    let c = abs(v);
    return sign(v) * select(pow((c + 0.055) / 1.055, vec3<f32>(2.4)), c / 12.92,
                            c <= vec3<f32>(0.04045));
}
