//! include adjustment_color
@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) { return; }
    let index = id.y * params.dimensions.x + id.x;
    let pixel = pixels[index];
    if (pixel.a <= 0.0) { pixels[index] = vec4<f32>(0.0); return; }
    let c = adjustment_encode_extended(pixel.rgb / pixel.a);
    let low = min(0.0, min(c.r, min(c.g, c.b)));
    let high = max(1.0, max(c.r, max(c.g, c.b)));
    let width = high - low;
    var hsl = adjustment_hsl((c - low) / width);
    if (params.reserved.x != 0u) {
        hsl.x = params.values.x;
        hsl.y = params.values.y;
    } else {
        hsl.x += params.values.x;
        hsl.y = min(hsl.y * (1.0 + params.values.y), 1.0);
    }
    hsl.z = select(hsl.z * (1.0 + params.values.z), mix(hsl.z, 1.0, params.values.z), params.values.z >= 0.0);
    let result = adjustment_rgb(hsl) * width + low;
    var linear = adjustment_decode_extended(result);
    pixels[index] = vec4<f32>(linear * pixel.a, pixel.a);
}
