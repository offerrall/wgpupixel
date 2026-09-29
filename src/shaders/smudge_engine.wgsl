// Smudge applies dabs strictly in order. A record either runs a chunk of small dabs in
// one workgroup, separated by storage barriers while lanes share each dab's patch, or
// spreads one large dab over many workgroups (dispatch boundaries order the dabs). The
// carried patch (side params.reserved.y, row stride params.reserved.w) moves with the
// dabs in whole pixels. Kernels define canvas_load/canvas_store: within one workgroup a
// pixel changes lanes between dabs, so those kernels access pixels atomically (plain
// loads may miss another lane's write in a separate cache); one-dab kernels need not. Dab words as in brush_engine; params.reserved.x counts this
// record's dabs and params.reserved.z holds flags (1 finger painting from params.color1,
// 2 the record starts the stroke, 4 one dab across workgroups). params.offsets clips
// writes to the region; params.values.x is strength.
fn smudge_slot(dab: u32, k: u32) {
    let side = params.reserved.y;
    let base = dab * 8u;
    let center = vec2<f32>(data[base], data[base + 1u]);
    let origin = vec2<i32>(floor(center)) - vec2<i32>(i32(side / 2u));
    let local = vec2<u32>(k % side, k / side);
    let pixel = origin + vec2<i32>(local);
    let slot = local.y * params.reserved.w + local.x;
    let inside = all(pixel >= vec2<i32>(0)) && all(pixel < vec2<i32>(params.dimensions.zw));
    var index = 0u;
    var current = vec4<f32>(0.0);
    if (inside) {
        index = u32(pixel.y) * params.dimensions.z + u32(pixel.x);
        current = canvas_load(index);
    }
    if (dab == 0u && (params.reserved.z & 2u) != 0u) {
        carried_store(slot, select(current, params.color1, (params.reserved.z & 1u) != 0u));
        return;
    }
    let paint = mix(current, carried_load(slot), params.values.x);
    carried_store(slot, paint);
    if (!inside || any(pixel < params.offsets.xy) || any(pixel >= params.offsets.zw)) { return; }
    let shape = mat2x2<f32>(data[base + 2u], data[base + 4u], data[base + 3u], data[base + 5u]);
    let amount = dab_coverage(vec2<f32>(pixel) + vec2<f32>(0.5) - center, shape) *
                 data[base + 6u] * data[base + 7u] * operation_coverage(index);
    if (amount > 0.0) {
        var result = mix(current, paint, amount);
        if ((params.reserved.z & 8u) != 0u) {
            if (current.a <= 0.0 || result.a <= 0.0) { return; }
            result = vec4<f32>(result.rgb * (current.a / result.a), current.a);
        }
        canvas_store(index, result);
    }
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(local_invocation_index) lane: u32,
        @builtin(global_invocation_id) id: vec3<u32>) {
    let side = params.reserved.y;
    if ((params.reserved.z & 4u) != 0u) {
        let position = id.xy + vec2<u32>(params.extra.xy);
        if (all(id.xy < vec2<u32>(params.extra.zw))) {
            smudge_slot(0u, position.y * side + position.x);
        }
        return;
    }
    let area = side * side;
    for (var dab = 0u; dab < params.reserved.x; dab += 1u) {
        for (var k = lane; k < area; k += 256u) {
            smudge_slot(dab, k);
        }
        storageBarrier();
    }
}
