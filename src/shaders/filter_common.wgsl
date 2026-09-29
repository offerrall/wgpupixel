fn straight(v: vec4<f32>) -> vec3<f32> {
    if (v.a <= 0.0) { return vec3<f32>(0.0); }
    return v.rgb / v.a;
}
fn sample_pixel(p: vec2<i32>) -> vec4<f32> {
    let q = vec2<u32>(clamp(p, vec2<i32>(0), vec2<i32>(params.dimensions.xy) - vec2<i32>(1)));
    return source[q.y * params.dimensions.x + q.x];
}
fn sample_linear(p: vec2<f32>) -> vec4<f32> {
    let q = clamp(p - vec2<f32>(0.5), vec2<f32>(0.0), vec2<f32>(params.dimensions.xy) - vec2<f32>(1.0));
    let lo = vec2<i32>(floor(q));
    let f = fract(q);
    let a = sample_pixel(lo); let b = sample_pixel(lo + vec2<i32>(1, 0));
    let c = sample_pixel(lo + vec2<i32>(0, 1)); let d = sample_pixel(lo + vec2<i32>(1, 1));
    let peak = max(max(abs(a), abs(b)), max(abs(c), abs(d)));
    if (any(peak > vec4<f32>(1e30))) {
        let scale = select(vec4<f32>(1.0), vec4<f32>(5.421010862427522e-20), peak > vec4<f32>(1e30));
        let result = mix(mix(a * scale, b * scale, f.x), mix(c * scale, d * scale, f.x), f.y);
        let limit = vec4<f32>(3.402823466e38) * scale;
        return clamp(result, -limit, limit) / scale;
    }
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
fn mean_four(a: vec4<f32>, b: vec4<f32>, c: vec4<f32>, d: vec4<f32>) -> vec4<f32> {
    let peak = max(max(abs(a), abs(b)), max(abs(c), abs(d)));
    let scale = select(vec4<f32>(1.0), vec4<f32>(5.421010862427522e-20), peak > vec4<f32>(1e30));
    let result = a * scale * 0.25 + b * scale * 0.25 + c * scale * 0.25 + d * scale * 0.25;
    let limit = vec4<f32>(3.402823466e38) * scale;
    return clamp(result, -limit, limit) / scale;
}
