// In-place brush tools, params.reserved.x: 0 color, 1 eraser, 2 dodge/burn, 3 sponge.
// reserved.y: blend mode or tone range; reserved.w: tool flags.
// Curves on sRGB-encoded values, extended beyond [0, 1]: the shadow and highlight
// curves are linear, midtones mirror the power for negatives, and burned shadows crush
// [0, e/3) to zero while negatives pass through.
fn tone_curve(v: f32, exposure: f32) -> f32 {
    let third = exposure / 3.0;
    let burn = (params.reserved.w & 1u) != 0u;
    switch params.reserved.y {
        case 0u: {
            if (burn) {
                if (v < 0.0) { return v; }
                return max(v - third, 0.0) / (1.0 - third);
            }
            return third + v - third * v;
        }
        case 2u: { return v * select(1.0 + third, 1.0 - third, burn); }
        default: {
            if (v == 0.0) { return 0.0; }
            return sign(v) * pow(abs(v), select(1.0 / (1.0 + exposure), 1.0 + third, burn));
        }
    }
}

fn dodge_burn(pixel: vec4<f32>, stroke: f32) -> vec4<f32> {
    if (pixel.a <= 0.0) { return pixel; }
    let color = pixel.rgb / pixel.a;
    var result: vec3<f32>;
    if ((params.reserved.w & 2u) != 0u) {
        // Protect tones: move luminance along the curve and keep channel ratios.
        let luma = dot(color, vec3<f32>(0.2126, 0.7152, 0.0722));
        let toned = srgb_decode(vec3<f32>(tone_curve(srgb_encode(vec3<f32>(luma)).x, stroke))).x;
        if (luma <= 0.0) {
            // No positive luminance to scale: shift all channels by the change instead.
            result = color + vec3<f32>(toned - luma);
        } else {
            result = color * (toned / luma);
            let top = max(max(result.r, result.g), result.b);
            let limit = max(1.0, max(max(color.r, color.g), color.b));
            if (top > limit && toned < limit) {
                // Pull chroma toward the target luminance instead of clipping.
                result = vec3<f32>(toned) + (result - vec3<f32>(toned)) *
                                                 ((limit - toned) / (top - toned));
            }
        }
    } else {
        let encoded = srgb_encode(color);
        result = srgb_decode(vec3<f32>(tone_curve(encoded.r, stroke), tone_curve(encoded.g, stroke),
                                       tone_curve(encoded.b, stroke)));
    }
    return vec4<f32>(result * pixel.a, pixel.a);
}

fn sponge(pixel: vec4<f32>, stroke: f32) -> vec4<f32> {
    if (pixel.a <= 0.0) { return pixel; }
    let color = pixel.rgb / pixel.a;
    let luma = dot(color, vec3<f32>(0.2126, 0.7152, 0.0722));
    if ((params.reserved.w & 1u) == 0u) {
        return vec4<f32>(mix(color, vec3<f32>(luma), stroke) * pixel.a, pixel.a);
    }
    let high = max(max(color.r, color.g), color.b);
    let low = min(min(color.r, color.g), color.b);
    let vibrance = (params.reserved.w & 2u) != 0u;
    var factor = 1.0 + stroke;
    if (vibrance && high > 0.0) {
        factor = 1.0 + stroke * (1.0 - clamp((high - low) / high, 0.0, 1.0));
    }
    if (low < luma && luma >= 0.0) { factor = min(factor, luma / (luma - low)); }
    if (vibrance && high > luma) { factor = min(factor, (max(1.0, high) - luma) / (high - luma)); }
    factor = max(factor, 1.0);
    return vec4<f32>((vec3<f32>(luma) + (color - vec3<f32>(luma)) * factor) * pixel.a, pixel.a);
}

fn paint_tool(pixel: vec4<f32>, stroke: f32) -> vec4<f32> {
    switch params.reserved.x {
        case 1u: { return pixel * (1.0 - stroke); }
        case 2u: { return dodge_burn(pixel, stroke); }
        case 3u: { return sponge(pixel, stroke); }
        default: { return painting_blend(params.color1, pixel, stroke, params.reserved.y); }
    }
}
