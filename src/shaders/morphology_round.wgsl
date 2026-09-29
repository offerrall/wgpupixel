//! include filter_common
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;
fn apply_operation(id: vec3<u32>) {
    let index = id.y * params.dimensions.x + id.x;
    let radius = params.offsets.x;
    if (radius == 0) { destination[index] = source[index]; return; }
    var color = source[index];
    for (var y = -radius; y <= radius; y += 1) {
        for (var x = -radius; x <= radius; x += 1) {
            if (x * x + y * y > radius * radius) { continue; }
            let value = sample_pixel(vec2<i32>(id.xy) + vec2<i32>(x, y));
            if (params.offsets.z == 1) { color = min(color, value); }
            else { color = max(color, value); }
        }
    }
    destination[index] = select(color, vec4<f32>(0.0), color.a == 0.0);
}
