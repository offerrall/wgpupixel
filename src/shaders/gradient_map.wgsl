//! include adjustment_color
@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;
@group(0) @binding(7) var<storage, read> data: array<f32>;

fn stop_color(i: u32) -> vec4<f32> {
    let offset = i * 7u;
    return vec4<f32>(data[offset+1u],data[offset+2u],data[offset+3u],data[offset+4u]);
}
fn stop_before(i: u32, position: f32, pixel: vec4<f32>) -> bool {
    // Compare neutral hard edges before unpremultiplication/encoding can round
    // across a stop. Thresholds are decoded on the CPU; dither uses encoded positions.
    if (pixel.r == pixel.g && pixel.g == pixel.b && params.reserved.z == 0u) {
        let gray = clamp(pixel.r, 0.0, pixel.a);
        if (params.reserved.y != 0u) { return gray <= data[i*7u+6u] * pixel.a; }
        return gray >= data[i*7u+5u] * pixel.a;
    }
    return position >= data[i*7u];
}
fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) { return; }
    let index = id.y * params.dimensions.x + id.x;
    let pixel = pixels[index];
    if (pixel.a <= 0.0) { pixels[index] = vec4<f32>(0.0); return; }
    var c = adjustment_encode(pixel.rgb / pixel.a);
    var position = adjustment_luma(c);
    if (c.r == c.g && c.g == c.b) { position = c.r; }
    if (params.reserved.y != 0u) { position = 1.0 - position; }
    if (params.reserved.z != 0u) {
        var hash = id.x * 1664525u + id.y * 1013904223u + 2246822519u;
        hash = (hash ^ (hash >> 16u)) * 2246822519u;
        position += (f32(hash & 65535u) / 65535.0 - 0.5) / 255.0;
    }
    var mapped = stop_color(0u);
    if (stop_before(0u, position, pixel)) {
        var left = 0u;
        var right = params.reserved.x;
        // Upper bound makes exact duplicates select their last stop.
        while (left < right) {
            let mid = left + (right - left) / 2u;
            if (stop_before(mid, position, pixel)) { left = mid + 1u; } else { right = mid; }
        }
        let previous = left - 1u;
        mapped = stop_color(previous);
        if (left < params.reserved.x) {
            let t = clamp((position - data[previous*7u]) / (data[left*7u] - data[previous*7u]), 0.0, 1.0);
            mapped = mix(mapped, stop_color(left), t);
        }
    }
    if (mapped.a == 0.0) { pixels[index] = vec4<f32>(0.0); return; }
    let alpha = mapped.a * pixel.a;
    pixels[index] = vec4<f32>(adjustment_decode(mapped.rgb / mapped.a) * alpha, alpha);
}
