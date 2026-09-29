fn lanczos_sinc(x: f32) -> f32 {
    if (abs(x) < 0.00001) {
        return 1.0;
    }
    let angle = 3.141592653589793 * x;
    return sin(angle) / angle;
}

fn lanczos_weight(distance: f32) -> f32 {
    let x = abs(distance);
    if (x >= 3.0) {
        return 0.0;
    }
    return lanczos_sinc(x) * lanczos_sinc(x / 3.0);
}

// Floor-based support includes all six taps for every fractional position.
// Preserve signed linear premultiplied RGBA, including alpha ringing.
fn sample_lanczos(position: vec2<f32>) -> vec4<f32> {
    let origin = vec2<i32>(floor(position));
    let fraction = position - vec2<f32>(origin);
    var sum = vec4<f32>(0.0);
    var total = 0.0;
    for (var y = -2; y <= 3; y = y + 1) {
        let wy = lanczos_weight(f32(y) - fraction.y);
        for (var x = -2; x <= 3; x = x + 1) {
            let weight = lanczos_weight(f32(x) - fraction.x) * wy;
            sum += sample_pixel(origin + vec2<i32>(x, y)) * weight;
            total += weight;
        }
    }
    return sum / total;
}
