//! include filter_common
//! coverage kernel
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;
@group(0) @binding(4) var<storage, read> guide: array<vec4<f32>>;
fn apply_operation(id: vec3<u32>) {
    let index = id.y * params.dimensions.x + id.x;
    let p = vec2<f32>(id.xy) + 0.5;
    let mode = params.offsets.w;
    var result = source[index];
    if (mode == 0) {
        result = sample_linear(p - params.values.xy) * 0.5 + sample_linear(p + params.values.xy) * 0.5;
    } else if (mode == 1) {
        let q = p - params.color1.xy;
        let a = params.values.x; let b = params.values.y;
        result = sample_linear(params.color1.xy + vec2<f32>(a*q.x-b*q.y, b*q.x+a*q.y)) * 0.5 +
                 sample_linear(params.color1.xy + vec2<f32>(a*q.x+b*q.y, -b*q.x+a*q.y)) * 0.5;
    } else if (mode == 2) {
        let scale = params.values.x;
        result = source[index] / (1.0 + scale) +
                 sample_linear(params.color1.xy + (p - params.color1.xy) * scale) * (scale / (1.0 + scale));
    } else if (mode == 3) {
        let a = sample_pixel(vec2<i32>(id.xy) - params.offsets.xy);
        let b = sample_pixel(vec2<i32>(id.xy) + params.offsets.xy);
        if (params.offsets.z == 1) { result = min(result, min(a, b)); }
        else { result = max(result, max(a, b)); }
        if (result.a == 0.0) { result = vec4<f32>(0.0); }
    } else {
        let center = guide[index];
        if (center.a == 0.0) { result = vec4<f32>(0.0); }
        else {
            var sum = vec3<f32>(0.0);
            var normalization = 0.0;
            for (var j = -1; j <= 1; j += 1) {
                let q = clamp(vec2<i32>(id.xy) + params.offsets.xy * j, vec2<i32>(0), vec2<i32>(params.dimensions.xy) - 1);
                let k = u32(q.y) * params.dimensions.x + u32(q.x);
                let neighbor = guide[k];
                if (neighbor.a == 0.0) { continue; }
                let delta = (straight(neighbor) - straight(center)) / params.values.x;
                let weight = exp(-0.5 * dot(delta, delta)) * neighbor.a;
                normalization += weight;
                if (weight > 0.0) { sum = mix(sum, straight(source[k]), weight / normalization); }
            }
            result = vec4<f32>(sum * center.a, center.a);
        }
    }
    let coverage = select(1.0, operation_coverage(index), params.reserved.x == 1u);
    if (coverage == 1.0) { destination[index] = result; }
    else if (coverage > 0.0) { destination[index] = mix(destination[index], result, coverage); }
}
