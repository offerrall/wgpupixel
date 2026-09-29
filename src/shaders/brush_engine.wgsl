// Stroke coverage from the staged dab list. Data words (f32, indices bitcast):
//   8 per dab: center x, y; inverse shape m00, m01, m10, m11; opacity cap; flow
//   at params.reserved.z: per-tile start offsets of the index list, one extra at the end
//   index list: dab numbers per tile, in stroke order
// params.offsets: tile grid origin x, y; tile size; tiles per row.
fn stroke_coverage(pixel: vec2<u32>) -> f32 {
    let tile = vec2<u32>(vec2<i32>(pixel) - params.offsets.xy) / u32(params.offsets.z);
    let entry = params.reserved.z + tile.y * u32(params.offsets.w) + tile.x;
    let last = bitcast<u32>(data[entry + 1u]);
    let position = vec2<f32>(pixel) + vec2<f32>(0.5);
    var stroke = 0.0;
    for (var k = bitcast<u32>(data[entry]); k < last; k += 1u) {
        let base = bitcast<u32>(data[k]) * 8u;
        let cap = data[base + 6u];
        if (stroke >= cap) { continue; }
        let offset = position - vec2<f32>(data[base], data[base + 1u]);
        let shape = mat2x2<f32>(data[base + 2u], data[base + 4u], data[base + 3u], data[base + 5u]);
        // Dabs raise coverage toward their opacity by flow, never above it.
        stroke += (cap - stroke) * data[base + 7u] * dab_coverage(offset, shape);
    }
    return stroke;
}
