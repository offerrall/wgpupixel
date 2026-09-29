@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) {
        return;
    }
    let index = id.y * params.dimensions.x + id.x;
    let spacing = params.values.z;
    let width = params.values.w;
    if (width == 0.0) {
        pixels[index] = params.color2;
        return;
    }
    if (width == spacing) {
        pixels[index] = params.color1;
        return;
    }
    let position = dot(vec2<f32>(id.xy), params.values.xy) + params.extra.x;
    // Center the modulo on the stripe to retain small negative edge distances
    // when spacing is very large; positive modulo would add a huge period.
    let centered = position - width * 0.5;
    let wrapped = centered - round(centered / spacing) * spacing;
    let distance = width * 0.5 - abs(wrapped);
    let coverage = smoothstep(-0.5, 0.5, distance);
    pixels[index] = mix(params.color2, params.color1, coverage);
}
