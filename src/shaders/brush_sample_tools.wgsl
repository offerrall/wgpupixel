// Brush tools reading an image, params.reserved.x: 0 clone (offset params.extra.xy),
// 1 pattern (inverse transform params.extra, params.extra2.xy), 2 blur, 3 sharpen.
// Normalized weights keep every partial sum a convex combination: no HDR overflow.
fn focus_blur(center: vec2<i32>) -> vec4<f32> {
    let weights = array<f32, 5>(0.0625, 0.25, 0.375, 0.25, 0.0625);
    let size = vec2<i32>(sample_size());
    var sum = vec4<f32>(0.0);
    for (var j = 0; j < 5; j += 1) {
        for (var i = 0; i < 5; i += 1) {
            let p = clamp(center + vec2<i32>(i - 2, j - 2), vec2<i32>(0), size - vec2<i32>(1));
            sum += weights[i] * weights[j] * sample_texel(u32(p.y) * u32(size.x) + u32(p.x));
        }
    }
    return sum;
}

// Unsharp masking of premultiplied color. Color is not clipped: HDR overshoot and
// negative undershoot are kept. When alpha leaves [0, 1] it is clamped and color scales
// with it, keeping the straight color; a result without alpha is transparent black.
fn focus_sharpen(pixel: vec4<f32>, blurred: vec4<f32>, stroke: f32) -> vec4<f32> {
    let sharp_alpha = pixel.a + (pixel.a - blurred.a) * stroke;
    if (!(sharp_alpha > 0.0)) { return vec4<f32>(0.0); }
    let alpha = min(sharp_alpha, 1.0);
    // Apply the alpha scale before combining, and halve the difference: intermediates
    // stay finite unless the result itself exceeds the float range.
    let scale = alpha / sharp_alpha;
    let half_detail = 0.5 * pixel.rgb - 0.5 * blurred.rgb;
    return vec4<f32>(pixel.rgb * scale + half_detail * (2.0 * stroke * scale), alpha);
}

fn sample_tool(pixel: vec4<f32>, stroke: f32, id: vec2<u32>) -> vec4<f32> {
    let position = vec2<f32>(id) + vec2<f32>(0.5);
    switch params.reserved.x {
        case 1u: {
            let m = params.extra;
            let q = vec2<f32>(m.x * position.x + m.z * position.y,
                              m.y * position.x + m.w * position.y) + params.extra2.xy;
            return painting_blend(sampled_bilinear(q, true), pixel, stroke, params.reserved.y);
        }
        case 2u: { return painting_alpha(mix(pixel, focus_blur(vec2<i32>(id)), stroke), pixel); }
        case 3u: { return painting_alpha(focus_sharpen(pixel, focus_blur(vec2<i32>(id)), stroke), pixel); }
        default: {
            return painting_blend(sampled_bilinear(position - params.extra.xy, false), pixel, stroke,
                               params.reserved.y);
        }
    }
}
