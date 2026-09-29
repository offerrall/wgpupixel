//! include filter_common
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;
fn apply_operation(id: vec3<u32>) {
    let index = id.y * params.dimensions.x + id.x;
    if (params.values.z == 0.0) { destination[index] = source[index]; return; }
    let p = vec2<f32>(id.xy) + vec2<f32>(0.5);
    let delta = p - params.values.xy;
    let amount = params.values.z;
    let extent = length(delta) * select(amount * 0.01745329252, amount * 0.01, params.reserved.x == 1u);
    let count = u32(max(ceil(extent * 2.0) + 1.0, 2.0));
    var result = vec4<f32>(0.0);
    for (var i = 0u; i < count; i += 1u) {
        let t = f32(i) / f32(count - 1u);
        var q = p - delta * (t * amount * 0.01);
        if (params.reserved.x == 0u) {
            let angle = (t - 0.5) * amount * 0.01745329252;
            q = params.values.xy + vec2<f32>(delta.x * cos(angle) - delta.y * sin(angle),
                                             delta.x * sin(angle) + delta.y * cos(angle));
        }
        result += sample_linear(q) / f32(count);
    }
    destination[index] = result;
}
