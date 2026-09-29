@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;

fn gaussian_weight(distance: u32) -> f32 {
    let d = f32(distance);
    if (params.values.x < d * 0.0625) {
        return 0.0;
    }
    let ratio = d / params.values.x;
    return exp(-0.5 * ratio * ratio);
}

fn apply_operation(id: vec3<u32>) {
    let width = params.dimensions.x;
    let height = params.dimensions.y;
    if (id.x >= width || id.y >= height) {
        return;
    }
    let index = id.y * width + id.x;
    var sum = source[index];
    // Scale channels independently only for extreme HDR magnitudes. A 2^-64
    // factor leaves room for every admitted radius without scaling a small alpha
    // alongside large RGB. Ordinary samples retain the original accumulation.
    var scale = vec4<f32>(1.0);
    var hdr = false;
    var normalization = 1.0;
    let radius = u32(params.offsets.z);
    for (var distance = 1u; distance <= radius; distance += 1u) {
        // This is the same zero-support test as gaussian_weight, performed
        // before reading neighbors. Avoid sigma*16, which can overflow.
        if (params.values.x < f32(distance) * 0.0625) { break; }
        let weight = gaussian_weight(distance);
        let top = id.y - min(id.y, distance);
        let bottom = id.y + min(height - 1u - id.y, distance);
        let first = source[top * width + id.x];
        let second = source[bottom * width + id.x];
        let large = max(abs(sum), max(abs(first), abs(second))) > vec4<f32>(1.0e30);
        if (hdr || any(large)) {
            hdr = true;
            let next_scale = select(scale, vec4<f32>(5.421010862427522e-20), large);
            if (any(next_scale != scale)) {
                sum *= next_scale / scale;
                scale = next_scale;
            }
            sum += (first * scale) * weight;
            sum += (second * scale) * weight;
        } else {
            // Keep the ordinary loop free of per-channel scale arithmetic.
            sum += first * weight;
            sum += second * weight;
        }
        normalization += 2.0 * weight;
    }
    if (hdr) {
        destination[index] = (sum / normalization) / scale;
    } else {
        destination[index] = sum / normalization;
    }
}
