//! include adjustment_color
@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;
@group(0) @binding(7) var<storage, read> data: array<f32>;

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) { return; }
    let index = id.y * params.dimensions.x + id.x;
    let pixel = pixels[index];
    if (pixel.a <= 0.0) { pixels[index] = vec4<f32>(0.0); return; }
    var c = adjustment_encode_extended(pixel.rgb / pixel.a);
    let l = adjustment_hsl(c).z;
    let shadows = 1.0 - clamp((l - (1.0/3.0 - 0.125)) * 4.0, 0.0, 1.0);
    let highlights = clamp((l - (2.0/3.0 - 0.125)) * 4.0, 0.0, 1.0);
    let midtones = 1.0 - shadows - highlights;
    var result = c + 0.7 * (
        vec3<f32>(data[0],data[1],data[2]) * shadows +
        vec3<f32>(data[3],data[4],data[5]) * midtones +
        vec3<f32>(data[6],data[7],data[8]) * highlights);
    if (params.reserved.x != 0u) { result = adjustment_preserve_extended(result, c); }
    var linear = adjustment_decode_extended(result);
    pixels[index] = vec4<f32>(linear * pixel.a, pixel.a);
}
