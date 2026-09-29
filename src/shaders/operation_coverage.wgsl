@group(0) @binding(3) var<storage, read> coverage_mask: array<u32>;

fn operation_coverage(index: u32) -> f32 {
    let word = coverage_mask[index / 4u];
    return f32((word >> ((index % 4u) * 8u)) & 255u) / 255.0;
}
