//! include filter_common
//! coverage kernel
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;
fn accumulate(sum: ptr<function, vec4<f32>>, correction: ptr<function, vec4<f32>>, value: vec4<f32>) {
    let next = *sum + value;
    *correction += select((value - next) + *sum, (*sum - next) + value, abs(*sum) >= abs(value));
    *sum = next;
}
fn apply_operation(id: vec3<u32>) {
    let vertical = params.offsets.y == 1;
    let start = select(vec2<u32>(id.x * 128u, id.y), vec2<u32>(id.x, id.y * 128u), vertical);
    if (any(start >= params.dimensions.xy)) { return; }
    let step = select(vec2<i32>(1, 0), vec2<i32>(0, 1), vertical);
    let radius = params.offsets.x;
    let weight = 0.5 / f32(2 * radius + 1);
    var sum = vec4<f32>(0.0);
    var correction = vec4<f32>(0.0);
    for (var offset = -radius; offset <= radius; offset += 1) {
        accumulate(&sum, &correction, sample_pixel(vec2<i32>(start) + step * offset) * weight);
    }
    let count = min(128u, select(params.dimensions.x - start.x, params.dimensions.y - start.y, vertical));
    for (var i = 0u; i < count; i += 1u) {
        let p = start + vec2<u32>(step) * i;
        let index = p.y * params.dimensions.x + p.x;
        var coverage = 1.0;
        if (vertical) {
            coverage = 0.0;
            if (all(p >= params.reserved.xy) && all(p < params.reserved.zw)) {
                coverage = operation_coverage(index);
            }
        }
        if (coverage > 0.0) {
            let value = clamp(sum + correction, vec4<f32>(-1.701411733e38), vec4<f32>(1.701411733e38)) * 2.0;
            if (coverage == 1.0) { destination[index] = value; }
            else { destination[index] = mix(destination[index], value, coverage); }
        }
        if (i + 1u < count) {
            let added = sample_pixel(vec2<i32>(p) + step * (radius + 1)) * weight;
            let removed = sample_pixel(vec2<i32>(p) - step * radius) * weight;
            accumulate(&sum, &correction, -removed);
            accumulate(&sum, &correction, added);
        }
    }
}
