//! include filter_common
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;
fn apply_operation(id: vec3<u32>) {
    let index = id.y * params.dimensions.x + id.x;
    if (params.offsets.x == 0) { destination[index] = source[index]; return; }
    var p0 = sample_pixel(vec2<i32>(id.xy) + vec2<i32>(-1, -1));
    var p1 = sample_pixel(vec2<i32>(id.xy) + vec2<i32>(0, -1));
    var p2 = sample_pixel(vec2<i32>(id.xy) + vec2<i32>(1, -1));
    var p3 = sample_pixel(vec2<i32>(id.xy) + vec2<i32>(-1, 0));
    var p4 = sample_pixel(vec2<i32>(id.xy) + vec2<i32>(0, 0));
    var p5 = sample_pixel(vec2<i32>(id.xy) + vec2<i32>(1, 0));
    var p6 = sample_pixel(vec2<i32>(id.xy) + vec2<i32>(-1, 1));
    var p7 = sample_pixel(vec2<i32>(id.xy) + vec2<i32>(0, 1));
    var p8 = sample_pixel(vec2<i32>(id.xy) + vec2<i32>(1, 1));
    // Nine phases of the odd-even sorting network; 36 fixed vector comparisons.
    { let lo = min(p0, p1); p1 = max(p0, p1); p0 = lo; }
    { let lo = min(p2, p3); p3 = max(p2, p3); p2 = lo; }
    { let lo = min(p4, p5); p5 = max(p4, p5); p4 = lo; }
    { let lo = min(p6, p7); p7 = max(p6, p7); p6 = lo; }
    { let lo = min(p1, p2); p2 = max(p1, p2); p1 = lo; }
    { let lo = min(p3, p4); p4 = max(p3, p4); p3 = lo; }
    { let lo = min(p5, p6); p6 = max(p5, p6); p5 = lo; }
    { let lo = min(p7, p8); p8 = max(p7, p8); p7 = lo; }
    { let lo = min(p0, p1); p1 = max(p0, p1); p0 = lo; }
    { let lo = min(p2, p3); p3 = max(p2, p3); p2 = lo; }
    { let lo = min(p4, p5); p5 = max(p4, p5); p4 = lo; }
    { let lo = min(p6, p7); p7 = max(p6, p7); p6 = lo; }
    { let lo = min(p1, p2); p2 = max(p1, p2); p1 = lo; }
    { let lo = min(p3, p4); p4 = max(p3, p4); p3 = lo; }
    { let lo = min(p5, p6); p6 = max(p5, p6); p5 = lo; }
    { let lo = min(p7, p8); p8 = max(p7, p8); p7 = lo; }
    { let lo = min(p0, p1); p1 = max(p0, p1); p0 = lo; }
    { let lo = min(p2, p3); p3 = max(p2, p3); p2 = lo; }
    { let lo = min(p4, p5); p5 = max(p4, p5); p4 = lo; }
    { let lo = min(p6, p7); p7 = max(p6, p7); p6 = lo; }
    { let lo = min(p1, p2); p2 = max(p1, p2); p1 = lo; }
    { let lo = min(p3, p4); p4 = max(p3, p4); p3 = lo; }
    { let lo = min(p5, p6); p6 = max(p5, p6); p5 = lo; }
    { let lo = min(p7, p8); p8 = max(p7, p8); p7 = lo; }
    { let lo = min(p0, p1); p1 = max(p0, p1); p0 = lo; }
    { let lo = min(p2, p3); p3 = max(p2, p3); p2 = lo; }
    { let lo = min(p4, p5); p5 = max(p4, p5); p4 = lo; }
    { let lo = min(p6, p7); p7 = max(p6, p7); p6 = lo; }
    { let lo = min(p1, p2); p2 = max(p1, p2); p1 = lo; }
    { let lo = min(p3, p4); p4 = max(p3, p4); p3 = lo; }
    { let lo = min(p5, p6); p6 = max(p5, p6); p5 = lo; }
    { let lo = min(p7, p8); p8 = max(p7, p8); p7 = lo; }
    { let lo = min(p0, p1); p1 = max(p0, p1); p0 = lo; }
    { let lo = min(p2, p3); p3 = max(p2, p3); p2 = lo; }
    { let lo = min(p4, p5); p5 = max(p4, p5); p4 = lo; }
    { let lo = min(p6, p7); p7 = max(p6, p7); p6 = lo; }
    destination[index] = select(p4, vec4<f32>(0.0), p4.a == 0.0);
}
