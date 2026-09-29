//! include analysis
//! coverage kernel
//! entry none
// Counts per channel; params.reserved: bins, space, workgroups along x, channels per layer.
// Workgroup layer z accumulates channels [z * per_layer, (z + 1) * per_layer) locally, then
// adds its nonzero bins to the global counts, which submit cleared.
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> counts: array<atomic<u32>>;

var<workgroup> local_counts: array<atomic<u32>, 4096>;

@compute @workgroup_size(256, 1, 1)
fn main(@builtin(workgroup_id) group: vec3<u32>,
        @builtin(local_invocation_index) lane: u32) {
    let bins = params.reserved.x;
    let first = group.z * params.reserved.w;
    let channels = min(5u, first + params.reserved.w) - first;
    let used = channels * bins;
    for (var i = lane; i < used; i += 256u) {
        atomicStore(&local_counts[i], 0u);
    }
    workgroupBarrier();
    let total = params.dispatch.z * params.dispatch.w;
    let stride = params.reserved.z * 256u;
    let scale = f32(bins - 1u);
    var i = group.x * 256u + lane;
    while (i < total) {
        let index = measured_index(i);
        if (operation_coverage(index) >= 0.5) {
            var measured = measure(source[index]);
            for (var c = 0u; c < channels; c += 1u) {
                let channel = first + c;
                if (channel == 3u || measured.color) {
                    let bin = min(u32(clamp(measured.values[channel], 0.0, 1.0) * scale + 0.5),
                                  bins - 1u);
                    atomicAdd(&local_counts[c * bins + bin], 1u);
                }
            }
        }
        if (total - i <= stride) {
            break;
        }
        i += stride;
    }
    workgroupBarrier();
    for (var i = lane; i < used; i += 256u) {
        let count = atomicLoad(&local_counts[i]);
        if (count != 0u) {
            atomicAdd(&counts[first * bins + i], count);
        }
    }
}
