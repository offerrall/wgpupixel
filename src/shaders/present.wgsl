//! coverage none
//! entry none
//! include viewport
struct Display { width: u32, height: u32, srgb_target: u32, padding: u32 }
@group(0) @binding(0) var<storage, read> pixels: array<vec4<f32>>;
@group(0) @binding(1) var<uniform> display: Display;

struct Vertex { @builtin(position) position: vec4<f32>, @location(0) uv: vec2<f32> }
@vertex fn vertex(@builtin(vertex_index) index: u32) -> Vertex {
    let points = array(vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    var out: Vertex;
    out.position = vec4(points[index], 0.0, 1.0);
    out.uv = points[index] * vec2(0.5, -0.5) + vec2(0.5);
    return out;
}

fn pixel(position: vec2<i32>) -> vec4<f32> {
    let size = vec2<i32>(i32(display.width), i32(display.height));
    let p = vec2<u32>(clamp(position, vec2<i32>(0), size - vec2<i32>(1)));
    return pixels[p.y * display.width + p.x];
}
fn encode(linear: vec3<f32>) -> vec3<f32> {
    return select(1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055,
                  12.92 * linear, linear <= vec3(0.0031308));
}
fn decode(encoded: vec3<f32>) -> vec3<f32> {
    return select(pow((encoded + 0.055) / 1.055, vec3(2.4)),
                  encoded / 12.92, encoded <= vec3(0.04045));
}
@fragment fn fragment(in: Vertex) -> @location(0) vec4<f32> {
    let p = in.uv * vec2<f32>(f32(display.width), f32(display.height)) - vec2(0.5);
    let base = vec2<i32>(floor(p));
    let f = fract(p);
    let rgba = mix(mix(pixel(base), pixel(base + vec2(1, 0)), f.x),
                   mix(pixel(base + vec2(0, 1)), pixel(base + vec2(1, 1)), f.x), f.y);
    let alpha = clamp(rgba.a, 0.0, 1.0);
    if (alpha <= 0.0) { return vec4(0.0); }
    let encoded = encode(clamp(rgba.rgb / alpha, vec3(0.0), vec3(1.0))) * alpha;
    // The sRGB attachment performs its own encode after the fragment shader.
    return vec4(select(encoded, decode(encoded), display.srgb_target != 0u), alpha);
}
