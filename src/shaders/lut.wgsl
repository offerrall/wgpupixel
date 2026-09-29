//! include adjustment_color
@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;
@group(0) @binding(7) var<storage, read> data: array<f32>;

fn lookup(x: f32, channel: u32) -> f32 {
    let n = params.reserved[channel];
    if (n == 0u) { return x; }
    var offset = 0u;
    for (var i = 0u; i < channel; i += 1u) { offset += params.reserved[i]; }
    if (x < 0.0) { return data[offset] + x * params.extra[channel]; }
    if (x > 1.0) { return data[offset+n-1u] + (x-1.0) * params.extra2[channel]; }
    let position = x * f32(n-1u);
    let low = min(u32(position), n-2u);
    return mix(data[offset+low], data[offset+low+1u], position-f32(low));
}
fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) { return; }
    let index = id.y * params.dimensions.x + id.x;
    let pixel = pixels[index];
    if (pixel.a <= 0.0) { pixels[index] = vec4<f32>(0.0); return; }
    var c = adjustment_encode_extended(pixel.rgb / pixel.a);
    if (params.offsets.x != 0) { c = pixel.rgb / pixel.a; }
    var result = c;
    for (var channel = 0u; channel < 3u; channel += 1u) {
        result[channel] = lookup(lookup(c[channel], channel + 1u), 0u);
    }
    var linear = result;
    if (params.offsets.x == 0) { linear = adjustment_decode_extended(result); }
    var output = linear * pixel.a;
    for (var channel = 0u; channel < 3u; channel += 1u) {
        if (params.reserved[0] == 0u && params.reserved[channel+1u] == 0u) {
            output[channel] = pixel[channel];
        }
    }
    pixels[index] = vec4<f32>(output, pixel.a);
}
