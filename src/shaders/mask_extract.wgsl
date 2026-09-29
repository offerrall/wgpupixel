//! coverage none
//! entry none
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<u32>;
@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    if (id.x >= params.dimensions.z || id.y >= params.dimensions.w) { return; }
    let word = id.y * params.dimensions.z + id.x;
    let count = params.dimensions.x * params.dimensions.y;
    if (word >= count / 4u + min(count % 4u, 1u)) { return; }
    var packed = 0u;
    for (var lane = 0u; lane < 4u; lane++) {
        let index = word * 4u + lane;
        if (index < count) {
            let pixel = source[index];
            var value = pixel.a;
            if (params.reserved.x == 1u) {
                value = dot(pixel.rgb, vec3<f32>(0.2126, 0.7152, 0.0722));
                if (pixel.a <= 0.0) { value = 0.0; }
            }
            packed |= u32(floor(clamp(value, 0.0, 1.0) * 255.0 + 0.5)) << (lane * 8u);
        }
    }
    destination[word] = packed;
}
