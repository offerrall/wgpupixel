// Catmull-Rom reconstruction of linear premultiplied RGBA. sample_pixel
// supplies the caller's boundary policy. Preserve signed/HDR ringing.
fn bicubic_weight(distance: f32) -> f32 {
    let x = abs(distance);
    if (x <= 1.0) {
        return ((1.5 * x - 2.5) * x) * x + 1.0;
    }
    if (x < 2.0) {
        return ((-0.5 * x + 2.5) * x - 4.0) * x + 2.0;
    }
    return 0.0;
}

fn sample_bicubic(position: vec2<f32>) -> vec4<f32> {
    let origin = vec2<i32>(floor(position));
    let fraction = position - vec2<f32>(origin);
    var sum = vec4<f32>(0.0);
    var total = 0.0;
    for (var y = -1; y <= 2; y = y + 1) {
        let wy = bicubic_weight(f32(y) - fraction.y);
        for (var x = -1; x <= 2; x = x + 1) {
            let weight = bicubic_weight(f32(x) - fraction.x) * wy;
            sum += sample_pixel(origin + vec2<i32>(x, y)) * weight;
            total += weight;
        }
    }
    return sum / total;
}
