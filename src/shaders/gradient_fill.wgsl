//! include painting_blend painting_color blend_modes blend_pixel blend_add blend_soft_light blend_hard_light
@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;
// Stops as (position, r, g, b, a) quintuples; params.reserved.z counts them.
@group(0) @binding(7) var<storage, read> data: array<f32>;

// params.values: start, end - start; params.extra: 1 / |d|^2, 1 / |d|, opacity.
// params.reserved: shape, blend mode, stop count, flags (1 reverse, 2 dither,
// 4 perceptual, edge mode << 4).
fn ramp_parameter(position: vec2<f32>) -> f32 {
    let v = position - params.values.xy;
    let axis = params.values.zw;
    let along = dot(v, axis);
    let across = axis.x * v.y - axis.y * v.x;
    switch params.reserved.x {
        case 1u: { return length(v) * params.extra.y; }
        case 2u: {
            // Positive turns follow clockwise image coordinates.
            let turn = atan2(across, along) / 6.283185307179586;
            return turn - floor(turn);
        }
        case 3u: { return abs(along) * params.extra.x; }
        case 4u: { return (abs(along) + abs(across)) * params.extra.x; }
        default: { return along * params.extra.x; }
    }
}

fn stop_position(stop: u32) -> f32 { return data[5u * stop]; }
fn stop_color(stop: u32) -> vec4<f32> {
    let base = 5u * stop;
    return vec4<f32>(data[base + 1u], data[base + 2u], data[base + 3u], data[base + 4u]);
}

fn perceptual_mix(a: vec4<f32>, b: vec4<f32>, weight: f32) -> vec4<f32> {
    let alpha = mix(a.a, b.a, weight);
    if (alpha <= 0.0) { return vec4<f32>(0.0); }
    let first = select(vec3<f32>(0.0), srgb_encode(a.rgb / a.a) * a.a, a.a > 0.0);
    let second = select(vec3<f32>(0.0), srgb_encode(b.rgb / b.a) * b.a, b.a > 0.0);
    return vec4<f32>(srgb_decode(mix(first, second, weight) / alpha) * alpha, alpha);
}

fn ramp_color(t: f32) -> vec4<f32> {
    let count = params.reserved.z;
    var low = 0u;
    var high = count;
    while (low < high) {
        let middle = (low + high) / 2u;
        if (stop_position(middle) > t) { high = middle; } else { low = middle + 1u; }
    }
    if (low == 0u) { return stop_color(0u); }
    if (low == count) { return stop_color(count - 1u); }
    let a = stop_position(low - 1u);
    let weight = (t - a) / (stop_position(low) - a);
    if ((params.reserved.w & 4u) != 0u) {
        return perceptual_mix(stop_color(low - 1u), stop_color(low), weight);
    }
    return mix(stop_color(low - 1u), stop_color(low), weight);
}

// Triangular noise of one 8-bit sRGB step on straight color and partial alpha.
fn dithered(color: vec4<f32>, pixel: vec2<u32>) -> vec4<f32> {
    if (color.a <= 0.0) { return color; }
    let key = (pixel.x * 0x1f123bb5u) ^ (pixel.y * 0x5f356495u);
    var noise = vec4<f32>(0.0);
    for (var c = 0u; c < 4u; c += 1u) {
        noise[c] = painting_random(key + c * 0x9e3779b9u) +
                   painting_random(key + c * 0x9e3779b9u + 0x632be5abu) - 1.0;
    }
    var alpha = color.a;
    if (alpha < 1.0) { alpha = clamp(alpha + noise.a / 255.0, 0.0, 1.0); }
    let encoded = srgb_encode(color.rgb / color.a) + noise.rgb / 255.0;
    return vec4<f32>(srgb_decode(encoded) * alpha, alpha);
}

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) { return; }
    var t = ramp_parameter(vec2<f32>(id.xy) + vec2<f32>(0.5));
    switch (params.reserved.w >> 4u) & 3u {
        case 0u: { if (!(t >= 0.0 && t <= 1.0)) { return; } }
        case 2u: { t = t - floor(t); }
        case 3u: {
            let m = t - 2.0 * floor(t * 0.5);
            t = select(m, 2.0 - m, m > 1.0);
        }
        default: { t = clamp(t, 0.0, 1.0); }
    }
    if ((params.reserved.w & 1u) != 0u) { t = 1.0 - t; }
    var color = ramp_color(t);
    if ((params.reserved.w & 2u) != 0u) { color = dithered(color, id.xy); }
    let index = id.y * params.dimensions.x + id.x;
    if ((params.reserved.w & 8u) != 0u) {
        pixels[index] = painting_alpha(mix(pixels[index], color, params.extra.z), pixels[index]);
        return;
    }
    pixels[index] = painting_blend(color, pixels[index], params.extra.z, params.reserved.y);
}
