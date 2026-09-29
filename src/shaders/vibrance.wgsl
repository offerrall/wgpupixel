@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;

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
    if (params.values.x == 0.0) { return; }
    let scale = max(abs(pixel.r), max(abs(pixel.g), abs(pixel.b)));
    if (scale == 0.0) { return; }
    let orientation = sign(pixel.a);
    let rgb = (pixel.rgb / scale) * orientation;
    let low = min(rgb.r, min(rgb.g, rgb.b));
    let high = max(rgb.r, max(rgb.g, rgb.b));
    // Compare the original straight-RGB thresholds without dividing by alpha.
    let threshold = 0.00001 * abs(pixel.a);
    if (low < 0.0 || high * scale <= threshold) {
        return;
    }
    let chroma = high - low;
    let saturation = chroma / high;
    if (saturation < 0.001) {
        return;
    }
    var hue = 0.0;
    if (chroma * scale >= threshold) {
        if (high == rgb.r) {
            hue = (rgb.g - rgb.b) / chroma;
        } else if (high == rgb.g) {
            hue = (rgb.b - rgb.r) / chroma + 2.0;
        } else {
            hue = (rgb.r - rgb.g) / chroma + 4.0;
        }
        hue = (hue - 6.0 * floor(hue / 6.0)) * 60.0;
    }
    var protection = 1.0;
    if (hue <= 50.0 || hue >= 330.0) {
        protection = 0.3;
    }
    let factor = 1.0 + params.values.x * (1.0 - saturation) * protection;
    let result = vec3<f32>(high) - (vec3<f32>(high) - rgb) * factor;
    pixels[index] = vec4<f32>(result * orientation * scale, pixel.a);
}
