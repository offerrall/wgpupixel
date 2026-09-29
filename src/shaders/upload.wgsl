//! coverage none
// Transfer buffer rectangle (dimensions.xy) -> image (dimensions.zw) at offsets.xy.
// reserved.x selects the CPU layout: 0 RGBA8 sRGB, 1 RGBA16 sRGB, 2 linear premultiplied f32.
@group(0) @binding(0) var<storage, read> source: array<u32>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;

fn premultiplied_linear(pixel: vec4<f32>) -> vec4<f32> {
    let linear = select(pow((pixel.rgb + 0.055) / 1.055, vec3<f32>(2.4)),
                        pixel.rgb / 12.92, pixel.rgb <= vec3<f32>(0.04045));
    return vec4<f32>(linear * pixel.a, pixel.a);
}

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) {
        return;
    }
    let index = id.y * params.dimensions.x + id.x;
    let output = (id.y + u32(params.offsets.y)) * params.dimensions.z + id.x + u32(params.offsets.x);
    switch params.reserved.x {
        case 1u: {
            let pixel = vec4<f32>(unpack2x16unorm(source[2u * index]),
                                  unpack2x16unorm(source[2u * index + 1u]));
            destination[output] = premultiplied_linear(pixel);
        }
        case 2u: {
            let word = 4u * index;
            destination[output] = bitcast<vec4<f32>>(vec4<u32>(
                source[word], source[word + 1u], source[word + 2u], source[word + 3u]));
        }
        default: {
            destination[output] = premultiplied_linear(unpack4x8unorm(source[index]));
        }
    }
}
