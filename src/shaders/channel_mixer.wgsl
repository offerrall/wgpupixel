//! include adjustment_color
@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;
@group(0) @binding(7) var<storage, read> data: array<f32>;

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) { return; }
    let index = id.y * params.dimensions.x + id.x;
    let pixel = pixels[index];
    if (pixel.a <= 0.0) { pixels[index] = vec4<f32>(0.0); return; }
    var c = adjustment_encode_extended(pixel.rgb / pixel.a);
    var result = vec3<f32>(0.0);
    for (var channel = 0u; channel < 3u; channel += 1u) {
        let offset = select(channel * 4u, 0u, params.reserved.x != 0u);
        result[channel] = dot(c, vec3<f32>(data[offset], data[offset+1u], data[offset+2u])) + data[offset+3u];
    }
    var linear = adjustment_decode_extended(result);
    pixels[index] = vec4<f32>(linear * pixel.a, pixel.a);
}
