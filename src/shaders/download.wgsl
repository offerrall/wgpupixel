//! coverage none
// Image (dimensions.xy) rectangle at offsets.xy -> transfer buffer rectangle (dimensions.zw).
// reserved.x selects the CPU layout: 0 RGBA8 sRGB, 1 RGBA16 sRGB, 2 linear premultiplied f32.
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<u32>;

// Clip only at export, after recovering straight RGB. Alpha is never gamma encoded.
fn straight_srgb(pixel: vec4<f32>) -> vec4<f32> {
    if (pixel.a <= 0.0) {
        return vec4<f32>(0.0);
    }
    let linear = clamp(pixel.rgb / pixel.a, vec3<f32>(0.0), vec3<f32>(1.0));
    let srgb = select(1.055 * pow(linear, vec3<f32>(1.0 / 2.4)) - 0.055,
                      12.92 * linear, linear <= vec3<f32>(0.0031308));
    return vec4<f32>(srgb, clamp(pixel.a, 0.0, 1.0));
}

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.z || id.y >= params.dimensions.w) {
        return;
    }
    let index = id.y * params.dimensions.z + id.x;
    let pixel = source[(id.y + u32(params.offsets.y)) * params.dimensions.x + id.x +
                       u32(params.offsets.x)];
    switch params.reserved.x {
        case 1u: {
            let encoded = straight_srgb(pixel);
            destination[2u * index] = pack2x16unorm(encoded.rg);
            destination[2u * index + 1u] = pack2x16unorm(encoded.ba);
        }
        case 2u: {
            let words = bitcast<vec4<u32>>(pixel);
            destination[4u * index] = words.x;
            destination[4u * index + 1u] = words.y;
            destination[4u * index + 2u] = words.z;
            destination[4u * index + 3u] = words.w;
        }
        default: {
            destination[index] = pack4x8unorm(straight_srgb(pixel));
        }
    }
}
