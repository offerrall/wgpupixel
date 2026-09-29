// Painting uses the same blend-color/source-over math with an opaque backdrop for
// alpha lock, then restores the destination alpha (as in layer compositing).
fn painting_blend(source: vec4<f32>, backdrop: vec4<f32>, opacity: f32, mode: u32) -> vec4<f32> {
    if ((params.reserved.w & 256u) == 0u) { return blend_pixel(source, backdrop, opacity, mode); }
    if (backdrop.a <= 0.0 || source.a <= 0.0 || opacity <= 0.0) { return backdrop; }
    // Normal mode stays premultiplied, including tiny-alpha HDR backdrops.
    if (mode == 0u) {
        return vec4<f32>(source.rgb * (opacity * backdrop.a) +
                          backdrop.rgb * (1.0 - source.a * opacity), backdrop.a);
    }
    let premultiplied = source.rgb * opacity;
    let alpha = source.a * opacity;
    if (mode == 8u) {
        return vec4<f32>(backdrop.rgb + premultiplied * backdrop.a, backdrop.a);
    }
    if (mode == 1u) {
        return vec4<f32>(backdrop.rgb * (1.0 - alpha) + backdrop.rgb * premultiplied, backdrop.a);
    }
    if (mode == 2u || mode == 7u) {
        let coefficient = select(1.0, 2.0, mode == 7u);
        return vec4<f32>(backdrop.rgb + premultiplied * backdrop.a -
            coefficient * (backdrop.rgb * premultiplied), backdrop.a);
    }
    let color = blend_pixel(source, vec4<f32>(backdrop.rgb / backdrop.a, 1.0), opacity, mode);
    return vec4<f32>(color.rgb * backdrop.a, backdrop.a);
}
fn painting_alpha(result: vec4<f32>, backdrop: vec4<f32>) -> vec4<f32> {
    if ((params.reserved.w & 256u) == 0u) { return result; }
    if (backdrop.a <= 0.0 || result.a <= 0.0) { return backdrop; }
    return vec4<f32>(result.rgb * (backdrop.a / result.a), backdrop.a);
}
