// Full u32 product, low word first. The 16-bit partial products and carries
// fit u32 individually; WebGPU does not require shader integer types wider
// than 32 bits.
fn resize_product(a: u32, b: u32) -> vec2<u32> {
    let low = (a & 65535u) * (b & 65535u);
    let middle = (a >> 16u) * (b & 65535u) + (low >> 16u);
    let upper_middle = (middle & 65535u) + (a & 65535u) * (b >> 16u);
    let high = (a >> 16u) * (b >> 16u) + (middle >> 16u) + (upper_middle >> 16u);
    return vec2<u32>((upper_middle << 16u) | (low & 65535u), high);
}

fn resize_product_le(a: vec2<u32>, b: vec2<u32>) -> bool {
    return a.y < b.y || (a.y == b.y && a.x <= b.x);
}

fn resize_nearest_coordinate(coordinate: u32, source_extent: u32, destination_extent: u32) -> u32 {
    // floor((coordinate + 0.5) * source_extent / destination_extent), without
    // rounding across a nearest-neighbor boundary. Exact ties select the
    // higher source coordinate. Extents fit positive i32, so doubled values
    // below fit u32 even at the resource API's largest admissible dimensions.
    let numerator_factor = 2u * coordinate + 1u;
    let denominator = 2u * destination_extent;
    if (source_extent <= 65535u && destination_extent <= 32768u) {
        return numerator_factor * source_extent / denominator;
    }
    let numerator = resize_product(numerator_factor, source_extent);
    // For large axes, start near the answer and correct using exact integer
    // comparisons. Float precision affects only the number of corrections,
    // never the selected pixel. All comparison products fit in two words.
    var result = min(u32((f32(coordinate) + 0.5) *
                        (f32(source_extent) / f32(destination_extent))), source_extent - 1u);
    while (result > 0u && !resize_product_le(resize_product(result, denominator), numerator)) {
        result -= 1u;
    }
    while (result < source_extent - 1u &&
           resize_product_le(resize_product(result + 1u, denominator), numerator)) {
        result += 1u;
    }
    return result;
}
