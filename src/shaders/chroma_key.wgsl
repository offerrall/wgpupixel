@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;

fn chroma_hsv(rgb: vec3<f32>) -> vec3<f32> {
    let high = max(rgb.r, max(rgb.g, rgb.b));
    let low = min(rgb.r, min(rgb.g, rgb.b));
    let delta = high - low;
    var hue = 0.0;
    var saturation = 0.0;
    if (high > 0.0) {
        saturation = delta / high;
    }
    if (delta > 0.0) {
        if (high == rgb.r) {
            hue = (rgb.g - rgb.b) / delta;
        } else if (high == rgb.g) {
            hue = (rgb.b - rgb.r) / delta + 2.0;
        } else {
            hue = (rgb.r - rgb.g) / delta + 4.0;
        }
        hue = (hue - 6.0 * floor(hue / 6.0)) / 6.0;
    }
    return vec3<f32>(hue, saturation, high);
}

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) {
        return;
    }
    let index = id.y * params.dimensions.x + id.x;
    let pixel = pixels[index];
    if (pixel.a == 0.0) {
        pixels[index] = vec4<f32>(0.0);
        return;
    }
    if (params.values.x == 0.0) {
        return;
    }
    var rgb = pixel.rgb / pixel.a;
    // HSV is defined here only for finite nonnegative straight RGB.
    if (any(rgb < vec3<f32>(0.0)) || any(abs(rgb) > vec3<f32>(3.402823466e38))) {
        return;
    }
    let hsv = chroma_hsv(rgb);
    let key_hsv = chroma_hsv(params.color1.rgb);
    let hue_difference = abs(hsv.x - key_hsv.x);
    let hue = min(hue_difference, 1.0 - hue_difference) * 2.0;
    let difference = vec3<f32>(hue * 1.41421356237, abs(hsv.y - key_hsv.y),
                               abs(hsv.z - key_hsv.z) * 0.70710678118);
    let scale = max(difference.x, max(difference.y, difference.z));
    var distance = 0.0;
    if (scale > 0.0) {
        distance = scale * length(difference / scale);
    }
    if (distance >= params.values.x) {
        return;
    }
    var coverage = 0.0;
    if (params.values.y > 0.0) {
        let start = max(0.0, params.values.x - params.values.y);
        let width = params.values.x - start;
        // Tiny positive smoothness may round away; treat that case as hard key.
        if (width > 0.0) {
            let t = clamp((distance - start) / width, 0.0, 1.0);
            coverage = t * t * (3.0 - 2.0 * t);
        }
    }
    if (coverage == 0.0) {
        pixels[index] = vec4<f32>(0.0);
        return;
    }
    if (params.values.z > 0.001 && coverage > 0.1 && coverage < 0.9) {
        let spill = max(vec3<f32>(0.0), rgb - params.color1.rgb * ((1.0 - coverage) * params.values.z));
        let weights = vec3<f32>(0.2126, 0.7152, 0.0722);
        let original_luma = dot(rgb, weights);
        let spill_luma = dot(spill, weights);
        if (spill_luma > 0.001) {
            rgb = (spill / spill_luma) * original_luma;
        }
    }
    let alpha = pixel.a * coverage;
    pixels[index] = vec4<f32>(rgb * alpha, alpha);
}
