// Shared by selection kernels. Selection masks pack four coverage bytes per word.
// Word kernels run 64 invocations per 8x8 workgroup over a linear index; the C++
// side sizes the dispatch for the invocation count it passes.
fn linear_index(group: vec3<u32>, groups: vec3<u32>, lane: u32) -> u32 {
    return (group.y * groups.x + group.x) * 64u + lane;
}

fn coverage_lane(word: u32, lane: u32) -> u32 {
    return (word >> (lane * 8u)) & 255u;
}

fn quantize(coverage: f32) -> u32 {
    return u32(floor(clamp(coverage, 0.0, 1.0) * 255.0 + 0.5));
}

// SelectionMode on bytes: replace, add, subtract, intersect, difference.
fn combine_coverage(existing: u32, incoming: u32, mode: u32) -> u32 {
    switch mode {
        case 1u: { return max(existing, incoming); }
        case 2u: { return select(0u, existing - incoming, existing > incoming); }
        case 3u: { return min(existing, incoming); }
        case 4u: { return max(existing, incoming) - min(existing, incoming); }
        default: { return incoming; }
    }
}

fn combine_word(existing: u32, incoming: vec4<u32>, mode: u32) -> u32 {
    var packed = 0u;
    for (var lane = 0u; lane < 4u; lane++) {
        packed |= combine_coverage(coverage_lane(existing, lane), incoming[lane], mode) << (lane * 8u);
    }
    return packed;
}

// Straight sRGB scaled to 0-255 per channel, alpha included; transparent is zero.
// HDR and negative linear values extend the sRGB curve (odd symmetry) instead of
// clipping, up to +-65535; alpha is coverage and stays in [0, 1].
fn straight_srgb255(pixel: vec4<f32>) -> vec4<f32> {
    if (pixel.a <= 0.0) {
        return vec4<f32>(0.0);
    }
    let linear = pixel.rgb / pixel.a;
    let magnitude = abs(linear);
    let encoded = select(1.055 * pow(magnitude, vec3<f32>(1.0 / 2.4)) - 0.055,
                         12.92 * magnitude, magnitude <= vec3<f32>(0.0031308));
    let srgb = clamp(sign(linear) * encoded * 255.0, vec3<f32>(-65535.0), vec3<f32>(65535.0));
    return vec4<f32>(srgb, clamp(pixel.a, 0.0, 1.0) * 255.0);
}

// The values an RGBA8 download of the pixel would produce, extended beyond 0-255 for
// HDR and negative colors.
fn straight_bytes(pixel: vec4<f32>) -> vec4<i32> {
    return vec4<i32>(floor(straight_srgb255(pixel) + 0.5));
}
