//! include selection_common
//! coverage none
//! entry none
// Coverage falls linearly with straight sRGB distance (0-255 scale) from color1.rgb,
// reaching zero at values.x, scaled by pixel alpha. reserved.x: selection mode.
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<u32>;

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(workgroup_id) group: vec3<u32>, @builtin(num_workgroups) groups: vec3<u32>,
        @builtin(local_invocation_index) lane: u32) {
    let word = linear_index(group, groups, lane);
    let count = params.dimensions.x * params.dimensions.y;
    if (word >= (count + 3u) / 4u) { return; }
    var incoming = vec4<u32>(0u);
    for (var k = 0u; k < 4u; k++) {
        let pixel = source[min(4u * word + k, count - 1u)];
        let color = straight_srgb255(pixel);
        let distance = length(color.rgb - params.color1.rgb);
        incoming[k] = quantize(clamp(1.0 - distance / params.values.x, 0.0, 1.0) * color.a / 255.0);
    }
    destination[word] = combine_word(destination[word], incoming, params.reserved.x);
}
