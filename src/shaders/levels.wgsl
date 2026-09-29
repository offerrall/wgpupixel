//! include adjustment_color
@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;
@group(0) @binding(7) var<storage, read> data: array<f32>;

fn level(x: f32, channel: u32) -> f32 {
    let offset = channel * 5u;
    let black = data[offset];
    let width = data[offset+1u] - black;
    let exponent = 1.0 / data[offset+2u];
    let boundary = clamp(x, 0.0, 1.0);
    let v = (boundary - black) / width;
    var mapped = pow(clamp(v, 0.0, 1.0), exponent);
    if (x != boundary) {
        let extended = (x - black) / width;
        mapped += sign(extended) * pow(abs(extended), exponent) - sign(v) * pow(abs(v), exponent);
    }
    return mix(data[offset+3u], data[offset+4u], mapped);
}
fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) { return; }
    let index = id.y * params.dimensions.x + id.x;
    let pixel = pixels[index];
    if (pixel.a <= 0.0) { pixels[index] = vec4<f32>(0.0); return; }
    var c = adjustment_encode_extended(pixel.rgb / pixel.a);
    var result = c;
    for (var channel = 0u; channel < 3u; channel += 1u) {
        result[channel] = level(level(c[channel], channel + 1u), 0u);
    }
    var linear = adjustment_decode_extended(result);
    pixels[index] = vec4<f32>(linear * pixel.a, pixel.a);
}
