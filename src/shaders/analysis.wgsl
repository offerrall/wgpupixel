// Measured values of one pixel: straight red, green, blue, alpha and luminosity in the
// space selected by params.reserved.y (0 sRGB-encoded and clipped, 1 linear).
struct Measurement {
    values: array<f32, 5>,
    color: bool, // Alpha > 0: color channels are defined.
};

fn measure(pixel: vec4<f32>) -> Measurement {
    var rgb = vec3<f32>(0.0);
    if (pixel.a > 0.0) {
        rgb = pixel.rgb / pixel.a;
    }
    var alpha = pixel.a;
    var luminosity = 0.0;
    if (params.reserved.y == 0u) {
        let linear = clamp(rgb, vec3<f32>(0.0), vec3<f32>(1.0));
        rgb = select(1.055 * pow(linear, vec3<f32>(1.0 / 2.4)) - 0.055, 12.92 * linear,
                     linear <= vec3<f32>(0.0031308));
        alpha = clamp(alpha, 0.0, 1.0);
        luminosity = dot(rgb, vec3<f32>(0.30, 0.59, 0.11));
    } else {
        luminosity = dot(rgb, vec3<f32>(0.2126, 0.7152, 0.0722));
    }
    return Measurement(array<f32, 5>(rgb.r, rgb.g, rgb.b, alpha, luminosity), pixel.a > 0.0);
}

// Measured rectangle params.dispatch as a linear range of pixels, visited by a grid of
// params.reserved.z workgroups of `lanes` invocations. Returns the image index of pixel i.
fn measured_index(i: u32) -> u32 {
    let width = params.dispatch.z;
    return (params.dispatch.y + i / width) * params.dimensions.x + params.dispatch.x + i % width;
}
