// Shared entry point for image operations. Each kernel defines apply_operation.
@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) local: vec3<u32>) {
    if (any(local.xy >= params.dispatch.zw)) { return; }
    apply_operation(vec3<u32>(local.xy + params.dispatch.xy, local.z));
}
