fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.z || id.y >= params.dimensions.w) { return; }
    let index = id.y * params.dimensions.z + id.x;
    let amount = stroke_coverage(id.xy) * operation_coverage(index);
    if (amount <= 0.0) { return; }
    let word = index / 4u;
    let shift = (index % 4u) * 8u;
    let original = f32((atomicLoad(&pixels[word]) >> shift) & 255u) / 255.0;
    let value = u32(floor(clamp(mix(original, params.values.x, amount), 0.0, 1.0) * 255.0 + 0.5));
    // Each invocation owns one byte, including when neighboring pixels share a word.
    atomicAnd(&pixels[word], ~(255u << shift));
    atomicOr(&pixels[word], value << shift);
}
