//! include filter_common
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;
//! coverage kernel
@group(0) @binding(7) var<storage, read> data: array<f32>;
fn apply_operation(id: vec3<u32>) {
    let index = id.y * params.dimensions.x + id.x;
    let radius = params.offsets.x;
    let direction = params.offsets.y;
    let mode = params.offsets.z;
    var coverage = 1.0;
    if (direction == 1 && mode != 3) { coverage = operation_coverage(index); }
    if (coverage == 0.0) { return; }
    var result = vec4<f32>(0.0);
    var extrema = source[index];
    for (var offset = -radius; offset <= radius; offset += 1) {
        let delta = select(vec2<i32>(offset, 0), vec2<i32>(0, offset), direction == 1);
        let value = sample_pixel(vec2<i32>(id.xy) + delta);
        if (mode == 0 || mode == 3) {
            var weight = 1.0 / f32(2 * radius + 1);
            if (mode == 3) { weight = data[u32(abs(offset))]; }
            result += value * weight;
        } else {
            let color = value;
            if (mode == 1) { extrema = min(extrema, color); }
            else { extrema = max(extrema, color); }
        }
    }
    if (mode == 3 && params.values.w == 2.0) { result = clamp(result, vec4<f32>(-1.701411733e38), vec4<f32>(1.701411733e38)) * 2.0; }
    if (mode == 1 || mode == 2) {
        result = select(extrema, vec4<f32>(0.0), extrema.a == 0.0);
    }
    if (coverage < 1.0) { result = mix(destination[index], result, coverage); }
    destination[index] = result;
}
