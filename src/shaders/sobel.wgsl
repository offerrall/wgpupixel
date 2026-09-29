@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;

fn sobel_luminance(position: vec2<i32>) -> f32 {
    let p = vec2<u32>(clamp(position, vec2<i32>(0), vec2<i32>(params.dimensions.xy) - vec2<i32>(1)));
    let pixel = source[p.y * params.dimensions.x + p.x];
    if (pixel.a == 0.0) {
        return 0.0;
    }
    return dot(pixel.rgb / pixel.a, vec3<f32>(0.2126, 0.7152, 0.0722));
}

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.z || id.y >= params.dimensions.w) {
        return;
    }
    let index = id.y * params.dimensions.z + id.x;
    let alpha = source[index].a;
    if (alpha == 0.0) {
        destination[index] = vec4<f32>(0.0);
        return;
    }
    let p = vec2<i32>(id.xy);
    let tl = sobel_luminance(p + vec2<i32>(-1, -1));
    let tc = sobel_luminance(p + vec2<i32>(0, -1));
    let tr = sobel_luminance(p + vec2<i32>(1, -1));
    let ml = sobel_luminance(p + vec2<i32>(-1, 0));
    let mr = sobel_luminance(p + vec2<i32>(1, 0));
    let bl = sobel_luminance(p + vec2<i32>(-1, 1));
    let bc = sobel_luminance(p + vec2<i32>(0, 1));
    let br = sobel_luminance(p + vec2<i32>(1, 1));
    let gradient = vec2<f32>((tr - tl) + 2.0 * (mr - ml) + (br - bl),
                             (bl - tl) + 2.0 * (bc - tc) + (br - tr));
    // Scale before length to avoid overflow from squaring large HDR gradients.
    let scale = max(abs(gradient.x), abs(gradient.y));
    var magnitude = 0.0;
    if (scale > 0.0) {
        magnitude = scale * length(gradient / scale);
    }
    destination[index] = vec4<f32>(vec3<f32>(magnitude * alpha), alpha);
}
