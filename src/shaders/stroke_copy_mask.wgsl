//! coverage none
// Exact region copy. Each invocation owns one byte; atomic updates preserve other
// bytes even when a rectangle or an odd-width row ends in the middle of a word.
@group(0) @binding(0) var<storage, read> source: array<u32>;
@group(0) @binding(1) var<storage, read_write> destination: array<atomic<u32>>;

fn apply_operation(id: vec3<u32>) {
    let index = id.y * params.dimensions.z + id.x;
    let word = index / 4u;
    let shift = (index % 4u) * 8u;
    let mask = 255u << shift;
    let value = source[word] & mask;
    atomicAnd(&destination[word], ~mask);
    atomicOr(&destination[word], value);
}
