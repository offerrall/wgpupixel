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
    // Hue rotation is homogeneous. Normalize premultiplied RGB directly so
    // tiny alpha and signed HDR cannot overflow unpremultiplication/chroma.
    let scale = max(abs(pixel.r), max(abs(pixel.g), abs(pixel.b)));
    if (scale == 0.0) { return; }
    let orientation = sign(pixel.a);
    let rgb = (pixel.rgb / scale) * orientation;
    let low = min(rgb.r, min(rgb.g, rgb.b));
    let high = max(rgb.r, max(rgb.g, rgb.b));
    let chroma = high - low;
    if (chroma == 0.0 || params.values.x == 0.0) {
        return;
    }
    var sector: f32;
    if (high == rgb.r) {
        sector = (rgb.g - rgb.b) / chroma;
    } else if (high == rgb.g) {
        sector = (rgb.b - rgb.r) / chroma + 2.0;
    } else {
        sector = (rgb.r - rgb.g) / chroma + 4.0;
    }
    sector += params.values.x / 60.0;
    sector -= 6.0 * floor(sector / 6.0);
    let second = chroma * (1.0 - abs((sector - 2.0 * floor(sector / 2.0)) - 1.0));
    var shifted: vec3<f32>;
    if (sector < 1.0) {
        shifted = vec3<f32>(chroma, second, 0.0);
    } else if (sector < 2.0) {
        shifted = vec3<f32>(second, chroma, 0.0);
    } else if (sector < 3.0) {
        shifted = vec3<f32>(0.0, chroma, second);
    } else if (sector < 4.0) {
        shifted = vec3<f32>(0.0, second, chroma);
    } else if (sector < 5.0) {
        shifted = vec3<f32>(second, 0.0, chroma);
    } else {
        shifted = vec3<f32>(chroma, 0.0, second);
    }
    pixels[index] = vec4<f32>((shifted + vec3<f32>(low)) * orientation * scale, pixel.a);
}
