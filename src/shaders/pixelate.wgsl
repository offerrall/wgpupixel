//! coverage kernel
//! entry none
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;
var<workgroup> sums: array<vec4<f32>, 64>;
@compute @workgroup_size(8, 8, 1)
fn main(@builtin(workgroup_id) group: vec3<u32>, @builtin(local_invocation_index) lane: u32) {
    let cell = params.reserved.x;
    let cell_index = group.y * u32(params.offsets.z) + group.x;
    let start = vec2<u32>(cell_index % params.reserved.w, cell_index / params.reserved.w) * cell;
    if (any(start >= params.dimensions.xy)) { return; }
    let end = min(start + vec2<u32>(cell), params.dimensions.xy);
    let width = end.x - start.x;
    let count = width * (end.y - start.y);
    var sum = vec4<f32>(0.0);
    for (var i = lane; i < count; i += 64u) {
        let p = start + vec2<u32>(i % width, i / width);
        sum += source[p.y * params.dimensions.x + p.x] * (0.5 / f32(count));
    }
    sums[lane] = sum;
    workgroupBarrier();
    for (var stride = 32u; stride > 0u; stride /= 2u) {
        if (lane < stride) { sums[lane] += sums[lane + stride]; }
        workgroupBarrier();
    }
    let mean = clamp(sums[0], vec4<f32>(-1.701411733e38), vec4<f32>(1.701411733e38)) * 2.0;
    for (var i = lane; i < count; i += 64u) {
        let p = start + vec2<u32>(i % width, i / width);
        if (p.x < params.reserved.y || p.y < params.reserved.z || p.x >= u32(params.offsets.x) || p.y >= u32(params.offsets.y)) { continue; }
        let index = p.y * params.dimensions.x + p.x;
        let coverage = operation_coverage(index);
        if (coverage == 1.0) { destination[index] = mean; }
        else if (coverage > 0.0) { destination[index] = mix(destination[index], mean, coverage); }
    }
}
