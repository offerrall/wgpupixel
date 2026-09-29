@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;

fn sharpen_pixel(position: vec2<i32>) -> vec3<f32> {
    let p = vec2<u32>(clamp(position, vec2<i32>(0), vec2<i32>(params.dimensions.xy) - vec2<i32>(1)));
    return source[p.y * params.dimensions.x + p.x].rgb;
}

fn sharpen_difference(center: vec3<f32>, neighbor: vec3<f32>) -> vec3<f32> {
    let strength = params.values.x;
    if (strength <= 1.0) {
        // Products cannot overflow here; subtracting unscaled opposite HDR
        // values could overflow even when their weighted difference fits.
        return center * strength - neighbor * strength;
    }
    // Subtract first for large strengths: equal HDR neighbors cancel exactly
    // without ever forming center * strength.
    return (center - neighbor) * strength;
}

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.z || id.y >= params.dimensions.w) {
        return;
    }
    let index = id.y * params.dimensions.z + id.x;
    let center = source[index];
    if (center.a == 0.0) {
        destination[index] = vec4<f32>(0.0);
        return;
    }
    if (params.values.x == 0.0) {
        destination[index] = center;
        return;
    }
    let p = vec2<i32>(id.xy);
    // Preserve the identity term separately from the zero-DC stencil.
    // Weight each difference before summing to avoid a large unweighted sum.
    var detail = sharpen_difference(center.rgb, sharpen_pixel(p + vec2<i32>(-1, 0)));
    detail += sharpen_difference(center.rgb, sharpen_pixel(p + vec2<i32>(1, 0)));
    detail += sharpen_difference(center.rgb, sharpen_pixel(p + vec2<i32>(0, -1)));
    detail += sharpen_difference(center.rgb, sharpen_pixel(p + vec2<i32>(0, 1)));
    destination[index] = vec4<f32>(center.rgb + detail, center.a);
}
