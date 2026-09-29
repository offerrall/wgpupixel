//! coverage kernel
//! entry none
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;
@group(0) @binding(7) var<storage, read> data: array<f32>;
// A 16x4 output tile plus up to 64 halo samples on each side of its long axis.
var<workgroup> tile: array<vec4<f32>, 576>;
@compute @workgroup_size(8, 8, 1)
fn main(@builtin(workgroup_id) group: vec3<u32>, @builtin(local_invocation_index) lane: u32) {
    let vertical = params.offsets.y == 1;
    let radius = params.offsets.z;
    let stride = 16u + 2u * u32(radius);
    let origin = select(vec2<u32>(group.x * 16u, group.y * 4u),
                        vec2<u32>(group.y * 16u, group.x * 4u), vertical);
    for (var i = lane; i < stride * 4u; i += 64u) {
        let q = vec2<i32>(origin) + vec2<i32>(i32(i % stride) - radius, i32(i / stride));
        let p = select(q, q.yx, vertical);
        let bounded = vec2<u32>(clamp(p, vec2<i32>(0), vec2<i32>(params.dimensions.xy) - vec2<i32>(1)));
        tile[i] = source[bounded.y * params.dimensions.x + bounded.x];
    }
    workgroupBarrier();
    let q = origin + vec2<u32>(lane % 16u, lane / 16u);
    let p = select(q, q.yx, vertical);
    if (any(p >= params.dimensions.xy)) { return; }
    let index = p.y * params.dimensions.x + p.x;
    var coverage = 1.0;
    if (vertical) {
        if (any(p < params.reserved.xy) || any(p >= params.reserved.zw)) { return; }
        coverage = operation_coverage(index);
        if (coverage == 0.0) { return; }
    }
    let center = (lane / 16u) * stride + lane % 16u + u32(radius);
    var sum = tile[center] * data[0];
    for (var distance = 1u; distance <= u32(radius); distance += 1u) {
        sum += tile[center - distance] * data[distance];
        sum += tile[center + distance] * data[distance];
    }
    let value = clamp(sum, vec4<f32>(-1.701411733e38), vec4<f32>(1.701411733e38)) * 2.0;
    if (coverage == 1.0) { destination[index] = value; }
    else { destination[index] = mix(destination[index], value, coverage); }
}
