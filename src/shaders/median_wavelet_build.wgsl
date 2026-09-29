//! coverage none
//! entry none
@group(0) @binding(0) var<storage, read> source: array<u32>;
@group(0) @binding(1) var<storage, read_write> destination: array<u32>;
@group(0) @binding(4) var<storage, read_write> matrix: array<vec2<u32>>;
@group(0) @binding(5) var<storage, read_write> coordinates: array<u32>;
@group(0) @binding(6) var<storage, read_write> totals: array<u32>;
var<workgroup> scan: array<u32, 64>;
var<workgroup> tile_bits: array<u32, 2112>;
fn inclusive(word: u32) -> u32 {
    let words = params.reserved.x / 32u;
    var total = matrix[u32(params.offsets.w) + word].y;
    if (word + 1u != words && word % 64u != 63u && word >= 64u) {
        total += matrix[u32(params.offsets.w) + (word / 64u) * 64u - 1u].y;
    }
    return total;
}
fn rank0(position: u32) -> u32 {
    let word = position / 32u;
    let bit = position % 32u;
    var total = 0u;
    if (word > 0u) { total = inclusive(word - 1u); }
    if (bit > 0u) { total += countOneBits(matrix[u32(params.offsets.w) + word].x & ((1u << bit) - 1u)); }
    return total;
}
@compute @workgroup_size(8, 8)
fn main(@builtin(workgroup_id) group: vec3<u32>, @builtin(local_invocation_index) lane: u32) {
    let index = (group.y * params.reserved.w + group.x) * 64u + lane;
    let count = params.reserved.x;
    let mode = params.offsets.x;
    if (mode == 0) {
        if (index >= count) { return; }
        let tile = bitcast<vec4<i32>>(params.color1);
        let x = index % params.reserved.y;
        let y = index / params.reserved.y;
        let p = clamp(vec2<i32>(i32(x), i32(y)) + tile.xy - tile.zz, vec2<i32>(0), vec2<i32>(params.dimensions.xy) - 1);
        let bits = source[(u32(p.y) * params.dimensions.x + u32(p.x)) * 4u + u32(params.offsets.z)];
        destination[index * 2u] = select(bits ^ 0x80000000u, ~bits, (bits & 0x80000000u) != 0u);
        destination[index * 2u + 1u] = select(params.reserved.y, x, y < params.reserved.z);
        return;
    }
    let value_mode = mode <= 2;
    if (!value_mode && source[count] != 0u) {
        if (mode == 4 && index == 0u) { destination[count] = 1u; }
        return;
    }
    if (mode == 1 || mode == 3) {
        let valid = index < count / 32u;
        var bits = 0u;
        let base = (group.y * params.reserved.w + group.x) * 2048u;
        for (var i = 0u; i < 32u; i += 1u) {
            let local = i * 64u + lane;
            var zero = 0u;
            if (base + local < count) {
                let value = source[(base + local) * select(1u, 2u, value_mode)];
                zero = select(0u, 1u, ((value >> u32(params.offsets.y)) & 1u) == 0u);
            }
            tile_bits[local + local / 32u] = zero;
        }
        workgroupBarrier();
        for (var b = 0u; b < 32u; b += 1u) { bits |= tile_bits[lane * 33u + b] << b; }
        scan[lane] = countOneBits(bits);
        workgroupBarrier();
        for (var shift = 1u; shift < 64u; shift *= 2u) {
            var add = 0u;
            if (lane >= shift) { add = scan[lane - shift]; }
            workgroupBarrier();
            scan[lane] += add;
            workgroupBarrier();
        }
        if (valid) { matrix[u32(params.offsets.w) + index] = vec2<u32>(bits, scan[lane]); }
        if (lane == 63u) { totals[group.y * params.reserved.w + group.x] = scan[lane]; }
        return;
    }
    if (index >= count) { return; }
    let value = source[index * select(1u, 2u, value_mode)];
    let zero = ((value >> u32(params.offsets.y)) & 1u) == 0u;
    let rank = rank0(index);
    let position = select(rank0(count) + index - rank, rank, zero);
    if (value_mode) {
        if (index == 0u) { coordinates[count] = select(0u, 1u, rank0(count) == 0u || rank0(count) == count); }
        let x = source[index * 2u + 1u];
        destination[position * 2u] = value;
        destination[position * 2u + 1u] = x;
        coordinates[index] = select(params.reserved.y, x, zero);
    } else {
        destination[position] = value;
        if (index == 0u) { destination[count] = 0u; }
    }
}
