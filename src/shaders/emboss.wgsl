@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;

fn emboss_color(position: vec2<i32>) -> vec3<f32> {
    let p = vec2<u32>(clamp(position, vec2<i32>(0), vec2<i32>(params.dimensions.xy) - vec2<i32>(1)));
    let pixel = source[p.y * params.dimensions.x + p.x];
    if (pixel.a == 0.0) {
        return vec3<f32>(0.0);
    }
    return pixel.rgb / pixel.a;
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
    if (params.values.x == 0.0) {
        destination[index] = vec4<f32>(vec3<f32>(0.5 * alpha), alpha);
        return;
    }
    let p = vec2<i32>(id.xy);
    let diagonal = emboss_color(p + vec2<i32>(1, 1)) - emboss_color(p + vec2<i32>(-1, -1));
    let horizontal = emboss_color(p + vec2<i32>(1, 0)) - emboss_color(p + vec2<i32>(-1, 0));
    let vertical = emboss_color(p + vec2<i32>(0, 1)) - emboss_color(p + vec2<i32>(0, -1));
    let sum = 2.0 * diagonal + horizontal + vertical + emboss_color(p);
    destination[index] = vec4<f32>((sum * params.values.x + vec3<f32>(0.5)) * alpha, alpha);
}
