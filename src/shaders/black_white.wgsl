//! include adjustment_color
@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;
@group(0) @binding(7) var<storage, read> data: array<f32>;

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) { return; }
    let index = id.y * params.dimensions.x + id.x;
    let pixel = pixels[index];
    if (pixel.a <= 0.0) { pixels[index] = vec4<f32>(0.0); return; }
    var c = adjustment_encode_extended(pixel.rgb / pixel.a);
    let sector = adjustment_hsl(c).x * 6.0;
    let low = u32(floor(sector)) % 6u;
    let weight = mix(data[low], data[(low+1u)%6u], fract(sector));
    let minimum = min(c.r, min(c.g, c.b));
    let gray = minimum + (max(c.r,max(c.g,c.b))-minimum)*weight;
    var result = vec3<f32>(gray);
    if (params.reserved.x != 0u) {
        let tint = adjustment_hsl(adjustment_encode(params.color1.rgb));
        let low = min(0.0, gray);
        let width = max(1.0, gray) - low;
        result = adjustment_rgb(vec3<f32>(tint.xy, (gray-low)/width)) * width + low;
    }
    var linear = adjustment_decode_extended(result);
    pixels[index] = vec4<f32>(linear * pixel.a, pixel.a);
}
