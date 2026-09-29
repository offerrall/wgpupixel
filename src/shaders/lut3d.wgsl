//! include adjustment_color
@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;
@group(0) @binding(7) var<storage, read> data: array<f32>;

fn cube(p: vec3<u32>) -> vec3<f32> {
    let n = params.reserved.x;
    let i = (p.x + n * (p.y + n * p.z)) * 3u;
    return vec3<f32>(data[i], data[i+1u], data[i+2u]);
}
fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) { return; }
    let index = id.y * params.dimensions.x + id.x;
    let pixel = pixels[index];
    if (pixel.a <= 0.0) { pixels[index] = vec4<f32>(0.0); return; }
    var c = pixel.rgb / pixel.a;
    if (params.offsets.x == 0) { c = adjustment_encode_extended(c); }
    c = (clamp(c, params.color1.rgb, params.color2.rgb) - params.color1.rgb) /
        (params.color2.rgb - params.color1.rgb);
    let n = params.reserved.x;
    let position = c * f32(n - 1u);
    let low = min(vec3<u32>(position), vec3<u32>(n - 2u));
    let t = position - vec3<f32>(low);
    let a = mix(cube(low), cube(low + vec3<u32>(1,0,0)), t.x);
    let b = mix(cube(low + vec3<u32>(0,1,0)), cube(low + vec3<u32>(1,1,0)), t.x);
    let d = mix(cube(low + vec3<u32>(0,0,1)), cube(low + vec3<u32>(1,0,1)), t.x);
    let e = mix(cube(low + vec3<u32>(0,1,1)), cube(low + vec3<u32>(1,1,1)), t.x);
    let result = mix(mix(a, b, t.y), mix(d, e, t.y), t.z);
    var linear = result;
    if (params.offsets.x == 0) { linear = adjustment_decode_extended(result); }
    pixels[index] = vec4<f32>(linear * pixel.a, pixel.a);
}
