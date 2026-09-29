fn stamp_coverage(pixel: vec2<u32>, stamp: u32) -> f32 {
    let center = vec2<i32>(data[2u * stamp], data[2u * stamp + 1u]);
    let distance = length(vec2<f32>(pixel) - vec2<f32>(center));
    let softness = max(1.0, params.values.x * (1.0 - params.values.y));
    if (distance >= params.values.x) { return 0.0; }
    if (distance <= params.values.x - softness) { return 1.0; }
    // Normalize before dividing: huge softness can flush its reciprocal to zero.
    let width = max(1.0 / params.values.x, 1.0 - params.values.y);
    let t = clamp((1.0 - distance / params.values.x) / width, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
