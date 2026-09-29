//! include painting_blend painting_color blend_modes blend_pixel blend_add blend_soft_light blend_hard_light
@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;

// sRGB-encoded straight color times alpha, and alpha: transparent pixels all match.
fn bucket_key(pixel: vec4<f32>) -> vec4<f32> {
    if (pixel.a <= 0.0) { return vec4<f32>(0.0); }
    let alpha = min(pixel.a, 1.0);
    return vec4<f32>(srgb_encode(pixel.rgb / pixel.a) * alpha, alpha);
}

// params.offsets.xy seed; values: tolerance, softness, opacity; reserved: mode, -, skip seed.
fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) { return; }
    let seed = vec2<u32>(params.offsets.xy);
    // The seed changes only in a second pass, after every pixel has compared with it.
    if (params.reserved.z != 0u && all(id.xy == seed)) { return; }
    let index = id.y * params.dimensions.x + id.x;
    let pixel = pixels[index];
    var coverage = 1.0;
    if (params.reserved.y == 0u) {
    let difference = abs(bucket_key(pixel) -
                         bucket_key(pixels[seed.y * params.dimensions.x + seed.x]));
    let distance = max(max(difference.r, difference.g), max(difference.b, difference.a));
    coverage = select(0.0, 1.0, distance <= params.values.x);
    if (params.values.y > 0.0) {
        coverage = 1.0 - smoothstep(params.values.x, params.values.x + params.values.y, distance);
    }
    }
    if (coverage <= 0.0) { return; }
    pixels[index] = painting_blend(params.color1, pixel, params.values.z * coverage,
                                params.reserved.x);
}
