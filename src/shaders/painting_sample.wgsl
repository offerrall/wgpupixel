// Bilinear sampling of the image behind sample_size() and sample_texel(), texel
// centers at integer + 0.5. wrap tiles it; otherwise the outside is transparent.
fn sampled_texel(texel: vec2<i32>, wrap: bool) -> vec4<f32> {
    let size = vec2<i32>(sample_size());
    var p = texel;
    if (wrap) {
        p = ((texel % size) + size) % size;
    } else if (any(texel < vec2<i32>(0)) || any(texel >= size)) {
        return vec4<f32>(0.0);
    }
    return sample_texel(u32(p.y) * u32(size.x) + u32(p.x));
}

fn sampled_bilinear(position: vec2<f32>, wrap: bool) -> vec4<f32> {
    let q = position - vec2<f32>(0.5);
    if (any(abs(q) > vec2<f32>(1e9))) { return vec4<f32>(0.0); }
    let origin = floor(q);
    let f = q - origin;
    let p = vec2<i32>(origin);
    return mix(mix(sampled_texel(p, wrap), sampled_texel(p + vec2<i32>(1, 0), wrap), f.x),
               mix(sampled_texel(p + vec2<i32>(0, 1), wrap),
                   sampled_texel(p + vec2<i32>(1, 1), wrap), f.x), f.y);
}
