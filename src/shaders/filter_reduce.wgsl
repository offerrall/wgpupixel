//! coverage none
//! include filter_common
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;
fn apply_operation(id: vec3<u32>) {
    let p = vec2<i32>(id.xy * 2u);
    let a = sample_pixel(p); let b = sample_pixel(p + vec2<i32>(1, 0));
    let c = sample_pixel(p + vec2<i32>(0, 1)); let d = sample_pixel(p + vec2<i32>(1, 1));
    var value = mean_four(a, b, c, d);
    if (params.offsets.x == 1) { value = min(min(a,b), min(c,d)); }
    if (params.offsets.x == 2) { value = max(max(a,b), max(c,d)); }
    if (params.offsets.x == 4) {
        let coverage = vec4<f32>(a.a, b.a, c.a, d.a);
        let positive = select(coverage, vec4<f32>(1.0), coverage == vec4<f32>(0.0));
        value = vec4<f32>(min(min(positive.x, positive.y), min(positive.z, positive.w)));
    }
    if (params.offsets.x == 3) {
        let ratio = vec2<f32>(params.dimensions.xy - 1u) / max(vec2<f32>(params.dimensions.zw - 1u), vec2<f32>(1.0));
        let center = vec2<f32>(id.xy) * ratio + 0.5;
        let offset = select(vec2<f32>(0.5), vec2<f32>(0.0), (id.xy == vec2<u32>(0u)) | (id.xy + 1u == params.dimensions.zw));
        value = mean_four(sample_linear(center - offset), sample_linear(center + offset),
                          sample_linear(center + offset * vec2<f32>(1.0, -1.0)),
                          sample_linear(center + offset * vec2<f32>(-1.0, 1.0)));
    }
    destination[id.y * params.dimensions.z + id.x] = value;
}
