fn blend_color(backdrop: vec3<f32>, foreground: vec3<f32>, mode: u32) -> vec3<f32> {
    switch mode {
        case 1u: { return backdrop * foreground; }
        case 2u: { return backdrop + foreground - backdrop * foreground; }
        case 3u: {
            return select(
                1.0 - 2.0 * (1.0 - backdrop) * (1.0 - foreground),
                2.0 * backdrop * foreground,
                backdrop <= vec3<f32>(0.5));
        }
        case 4u: { return min(backdrop, foreground); }
        case 5u: { return max(backdrop, foreground); }
        case 6u: { return abs(backdrop - foreground); }
        case 7u: { return backdrop + foreground - 2.0 * backdrop * foreground; }
        case 8u: { return blend_add(backdrop, foreground); }
        case 9u: { return blend_soft_light(backdrop, foreground); }
        case 10u: { return blend_hard_light(backdrop, foreground); }
        default: { return blend_extended(backdrop, foreground, mode); }
    }
}

fn blend_pixel(original: vec4<f32>, backdrop: vec4<f32>, opacity: f32, mode: u32) -> vec4<f32> {
    let pixel = original * opacity;
    if (pixel.a == 0.0) {
        return backdrop;
    }
    if (mode == 0u || backdrop.a == 0.0) {
        return pixel + backdrop * (1.0 - pixel.a);
    }

    if (mode == 8u) {
        // Add's source-over RGB simplifies to the premultiplied sum. Avoid
        // constructing straight HDR colors that can overflow for tiny alpha.
        let rgb = blend_add(backdrop.rgb, pixel.rgb);
        let alpha = pixel.a + backdrop.a * (1.0 - pixel.a);
        return vec4<f32>(rgb, alpha);
    }

    // Multiply's alpha-weighted overlap is exactly the premultiplied RGB
    // product. Avoid overflowing straight HDR colors (or underflowing a*b).
    if (mode == 1u) {
        let rgb = (1.0 - pixel.a) * backdrop.rgb +
                  (1.0 - backdrop.a) * pixel.rgb + backdrop.rgb * pixel.rgb;
        let alpha = pixel.a + backdrop.a * (1.0 - pixel.a);
        return vec4<f32>(rgb, alpha);
    }
    if (mode == 2u || mode == 7u) {
        // Source-over terms cancel the alpha-weighted linear terms of screen
        // and exclusion. Keep their product in premultiplied coordinates too.
        let coefficient = select(1.0, 2.0, mode == 7u);
        let rgb = backdrop.rgb + pixel.rgb - coefficient * (backdrop.rgb * pixel.rgb);
        let alpha = pixel.a + backdrop.a * (1.0 - pixel.a);
        return vec4<f32>(rgb, alpha);
    }

    // Blend straight colors only where both layers contribute, then compose
    // source-over in premultiplied form. Transparent pixels never divide by zero.
    let blended = blend_color(backdrop.rgb / backdrop.a, original.rgb / original.a, mode);
    let rgb = (1.0 - pixel.a) * backdrop.rgb +
              (1.0 - backdrop.a) * pixel.rgb + pixel.a * backdrop.a * blended;
    let alpha = pixel.a + backdrop.a * (1.0 - pixel.a);
    return vec4<f32>(rgb, alpha);
}
