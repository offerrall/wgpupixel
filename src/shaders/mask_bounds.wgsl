//! coverage none
//! entry none
// params.dispatch: clipped source rectangle; reserved: A8 threshold, first texel, count.
// Each group scans at most 4096 texels; each dispatch at most 256 groups (2^20 texels).
@group(0) @binding(0) var<storage, read> source: array<u32>;
@group(0) @binding(1) var<storage, read_write> bounds: array<atomic<u32>>;

var<workgroup> partials: array<vec4<u32>, 256>;

@compute @workgroup_size(256, 1, 1)
fn main(@builtin(workgroup_id) group: vec3<u32>,
        @builtin(local_invocation_index) lane: u32) {
    // Complemented minimum x/y, exclusive maximum x/y. Zero is the empty identity.
    var found = vec4<u32>(0u);
    let end = min((group.x + 1u) * 4096u, params.reserved.z);
    for (var local = group.x * 4096u + lane; local < end; local += 256u) {
        let i = params.reserved.y + local;
        let x = params.dispatch.x + i % params.dispatch.z;
        let y = params.dispatch.y + i / params.dispatch.z;
        let index = y * params.dimensions.x + x;
        let coverage = (source[index / 4u] >> ((index % 4u) * 8u)) & 255u;
        if (coverage >= params.reserved.x) {
            found = max(found, vec4<u32>(~x, ~y, x + 1u, y + 1u));
        }
    }
    partials[lane] = found;
    workgroupBarrier();
    for (var width = 128u; width > 0u; width /= 2u) {
        if (lane < width) {
            partials[lane] = max(partials[lane], partials[lane + width]);
        }
        workgroupBarrier();
    }
    if (lane == 0u && partials[0].z != 0u) {
        atomicMax(&bounds[0], partials[0].x);
        atomicMax(&bounds[1], partials[0].y);
        atomicMax(&bounds[2], partials[0].z);
        atomicMax(&bounds[3], partials[0].w);
    }
}
