//! include blend_modes blend_pixel blend_add blend_soft_light blend_hard_light
//! coverage kernel
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;
@group(0) @binding(4) var<storage, read> source_mask: array<u32>;

fn blend_range(value: f32, range: vec4<f32>) -> f32 {
    if (value < range.x || value > range.w) { return 0.0; }
    var coverage = 1.0;
    if (range.y > range.x) { coverage *= clamp((value - range.x) / (range.y - range.x), 0.0, 1.0); }
    if (range.w > range.z) { coverage *= clamp((range.w - value) / (range.w - range.z), 0.0, 1.0); }
    return coverage;
}
fn blend_if_gray(rgb: vec3<f32>) -> f32 {
    var c = clamp(rgb, vec3<f32>(0.0), vec3<f32>(1.0));
    if ((params.reserved.z & 8u) != 0u) {
        c = select(1.055 * pow(c, vec3<f32>(1.0 / 2.4)) - 0.055,
                   12.92 * c, c <= vec3<f32>(0.0031308));
    }
    return blend_lum(c);
}
fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.z || id.y >= params.dimensions.w) { return; }
    // Dispatch is clipped to the layer bounds; unsigned subtraction also handles negative offsets.
    let xy = id.xy - bitcast<vec2<u32>>(params.offsets.xy);
    if (any(xy >= params.dimensions.xy)) { return; }
    let si = xy.y * params.dimensions.x + xy.x;
    let di = id.y * params.dimensions.z + id.x;
    let original = source[si];
    let backdrop = destination[di];
    if (original.a == 0.0) { return; }
    var opacity = params.values.x;
    if (params.reserved.y != 0u) {
        opacity *= f32((source_mask[si / 4u] >> ((si % 4u) * 8u)) & 255u) / 255.0;
    }
    if ((params.reserved.z & 4u) != 0u) {
        var gray = 0.0;
        if (backdrop.a != 0.0) { gray = blend_if_gray(backdrop.rgb / backdrop.a); }
        opacity *= blend_range(blend_if_gray(original.rgb / original.a), params.color1) * blend_range(gray, params.color2);
    }
    let coverage = operation_coverage(di);
    if (opacity == 0.0 || coverage == 0.0) { return; }
    var result: vec4<f32>;
    if ((params.reserved.z & 1u) != 0u) {
        if (backdrop.a == 0.0) { return; }
        result = blend_sample(original, vec4<f32>(backdrop.rgb / backdrop.a, 1.0), opacity, params.reserved.x, xy, params.reserved.w) * backdrop.a;
        result.a = backdrop.a;
    } else {
        result = blend_sample(original, backdrop, opacity, params.reserved.x, xy, params.reserved.w);
    }
    var output = select(mix(backdrop, result, coverage), result, coverage == 1.0);
    if ((params.reserved.z & 1u) != 0u) { output.a = backdrop.a; }
    destination[di] = output;
}
