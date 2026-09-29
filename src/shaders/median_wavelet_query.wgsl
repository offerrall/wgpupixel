//! coverage kernel
@group(0) @binding(0) var<storage, read> matrix0: array<vec2<u32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;
@group(0) @binding(4) var<storage, read> matrix1: array<vec2<u32>>;
@group(0) @binding(5) var<storage, read> matrix2: array<vec2<u32>>;
@group(0) @binding(6) var<storage, read> matrix3: array<vec2<u32>>;
fn entry(plane: u32, word: u32) -> vec2<u32> {
    let index = (plane % params.reserved.w) * params.reserved.z + word;
    switch plane / params.reserved.w {
        case 0u: { return matrix0[index]; }
        case 1u: { return matrix1[index]; }
        case 2u: { return matrix2[index]; }
        default: { return matrix3[index]; }
    }
}
fn inclusive(plane: u32, word: u32) -> u32 {
    var total = entry(plane, word).y;
    if (word + 1u != params.reserved.z && word % 64u != 63u && word >= 64u) {
        total += entry(plane, (word / 64u) * 64u - 1u).y;
    }
    return total;
}
fn rank0(plane: u32, position: u32) -> u32 {
    let word = position / 32u;
    let bit = position % 32u;
    var total = 0u;
    if (word > 0u) { total = inclusive(plane, word - 1u); }
    if (bit > 0u) { total += countOneBits(entry(plane, word).x & ((1u << bit) - 1u)); }
    return total;
}
fn range_count(plane: u32, first: u32, last: u32, x0: u32, x1: u32, bits: u32) -> u32 {
    var ranges = vec4<u32>(first, last, first, last);
    var count = vec2<u32>(0u);
    for (var level = 0u; level < bits; level += 1u) {
        let p = plane + 1u + level;
        let z = vec4<u32>(rank0(p, ranges.x), rank0(p, ranges.y), rank0(p, ranges.z), rank0(p, ranges.w));
        let ones = ((vec2<u32>(x0, x1) >> vec2<u32>(bits - level - 1u)) & vec2<u32>(1u)) != vec2<u32>(0u);
        count += select(vec2<u32>(0u), z.yw - z.xz, ones);
        let zeros = inclusive(p, params.reserved.z - 1u);
        ranges = select(z, ranges - z + zeros, ones.xxyy);
    }
    return count.y - count.x;
}
fn apply_operation(id: vec3<u32>) {
    let radius = u32(params.offsets.x);
    let x = id.x - u32(params.offsets.y);
    let y = id.y - u32(params.offsets.z);
    let width = params.reserved.x;
    let bits = 32u - countLeadingZeros(width);
    var left = y * width;
    var right = (y + 2u * radius + 1u) * width;
    var rank = ((2u * radius + 1u) * (2u * radius + 1u)) / 2u;
    var key = 0u;
    for (var level = 0u; level < 32u; level += 1u) {
        let plane = level * (bits + 1u);
        let zeros = inclusive(plane, params.reserved.z - 1u);
        if (zeros == 0u) { key |= 1u << (31u - level); continue; }
        if (zeros == params.reserved.z * 32u) { continue; }
        let a = rank0(plane, left); let b = rank0(plane, right);
        if (b - a == right - left) { left = a; right = b; continue; }
        if (b == a) { left = left - a + zeros; right = right - b + zeros; key |= 1u << (31u - level); continue; }
        let count = range_count(plane, left, right, x, x + 2u * radius + 1u, bits);
        if (rank < count) { left = a; right = b; }
        else { rank -= count; left = left - a + zeros; right = right - b + zeros; key |= 1u << (31u - level); }
    }
    let value = bitcast<f32>(select(~key, key ^ 0x80000000u, (key & 0x80000000u) != 0u));
    let index = id.y * params.dimensions.z + id.x;
    let c = u32(params.offsets.w);
    let coverage = operation_coverage(index);
    if (coverage == 1.0) { destination[index][c] = value; }
    else if (coverage > 0.0) { destination[index][c] = mix(destination[index][c], value, coverage); }
}
