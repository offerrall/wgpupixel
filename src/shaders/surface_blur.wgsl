//! include filter_common
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;
fn apply_operation(id: vec3<u32>) {
    let index = id.y * params.dimensions.x + id.x;
    let radius = params.offsets.x;
    let threshold = params.values.x;
    if (radius == 0 || threshold == 0.0) { destination[index] = source[index]; return; }
    if (source[index].a == 0.0) { destination[index] = vec4<f32>(0.0); return; }
    let color = straight(source[index]);
    let sigma = max(f32(radius) * 0.5, 0.5);
    var sum = vec3<f32>(0.0);
    var normalization = 0.0;
    for (var y = -radius; y <= radius; y += 1) {
        for (var x = -radius; x <= radius; x += 1) {
            let value = sample_pixel(vec2<i32>(id.xy) + vec2<i32>(x, y));
            if (value.a == 0.0) { continue; }
            let difference = (straight(value) - color) / threshold;
            let weight = exp(-0.5 * (f32(x * x + y * y) / (sigma * sigma) + dot(difference, difference))) * value.a;
            // Online weighted mean avoids an unnormalized HDR sum.
            normalization += weight;
            if (weight > 0.0) { sum = mix(sum, straight(value), weight / normalization); }
        }
    }
    destination[index] = vec4<f32>(sum * source[index].a, source[index].a);
}
