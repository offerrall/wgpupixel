// Image kernels declare `source` and a constant border policy.
// Coordinates are measured at pixel centers.
fn sample_pixel(position: vec2<i32>) -> vec4<f32> {
    let edge = vec2<i32>(params.dimensions.xy) - vec2<i32>(1);
    let pixel = vec2<u32>(clamp(position, vec2<i32>(0), edge));
    let value = source[pixel.y * params.dimensions.x + pixel.x];
    let outside = any(position < vec2<i32>(0)) || any(position > edge);
    return select(value, vec4<f32>(0.0), transparent_border && outside);
}
fn sample_nearest(position: vec2<f32>) -> vec4<f32> {
    return sample_pixel(vec2<i32>(floor(position + vec2<f32>(0.5))));
}
fn sample_bilinear(position: vec2<f32>) -> vec4<f32> {
    let origin = floor(position);
    let f = position - origin;
    let p = vec2<i32>(origin);
    return mix(mix(sample_pixel(p), sample_pixel(p + vec2<i32>(1, 0)), f.x),
               mix(sample_pixel(p + vec2<i32>(0, 1)), sample_pixel(p + vec2<i32>(1, 1)), f.x), f.y);
}
fn sample_image(position: vec2<f32>, method: u32) -> vec4<f32> {
    switch method {
        case 0u: { return sample_nearest(position); }
        case 2u: { return sample_bicubic(position); }
        case 3u: { return sample_lanczos(position); }
        default: { return sample_bilinear(position); }
    }
}

fn sample_transform(position: vec2<f32>, method: u32) -> vec4<f32> {
    // Keep the filter's full support across the edge. Reject remote positions
    // before converting to integer taps, including very large zoom coordinates.
    let support = array<f32, 4>(0.5, 1.0, 2.0, 3.0)[method];
    let end = vec2<f32>(params.dimensions.xy) - vec2<f32>(1.0) + vec2<f32>(support);
    if (any(position < vec2<f32>(-support)) || any(position >= end)) {
        return vec4<f32>(0.0);
    }
    return sample_image(position, method);
}
