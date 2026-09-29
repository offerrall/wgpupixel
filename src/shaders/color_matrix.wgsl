//! include adjustment_color
@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;
@group(0) @binding(7) var<storage, read> data: array<f32>;
fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) { return; }
    let index = id.y * params.dimensions.x + id.x;
    let pixel = pixels[index];
    var c = vec3<f32>(0.0);
    if (pixel.a > 0.0) { c = pixel.rgb / pixel.a; }
    if (params.offsets.x == 0) { c = adjustment_encode_extended(c); }
    var result = vec4<f32>(0.0);
    for (var row = 0u; row < 4u; row += 1u) {
        let i = row * 5u;
        result[row] = dot(vec4<f32>(c, pixel.a), vec4<f32>(data[i],data[i+1u],data[i+2u],data[i+3u])) + data[i+4u];
    }
    let alpha = clamp(result.a, 0.0, 1.0);
    if (alpha == 0.0) { pixels[index] = vec4<f32>(0.0); return; }
    var linear = result.rgb;
    if (params.offsets.x == 0) { linear = adjustment_decode_extended(linear); }
    pixels[index] = vec4<f32>(linear * alpha, alpha);
}
