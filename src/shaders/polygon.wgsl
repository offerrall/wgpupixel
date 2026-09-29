@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) {
        return;
    }
    let size = vec2<f32>(params.dimensions.xy);
    let point = vec2<f32>(id.xy) + vec2<f32>(0.5) - size * 0.5;
    let rotated = vec2<f32>(point.x * params.values.x + point.y * params.values.y,
                            -point.x * params.values.y + point.y * params.values.x);
    let radius = min(size.x, size.y) * 0.5;
    let radial = length(rotated);
    var angle = 0.0;
    if (radial > 0.0) { angle = atan2(rotated.x, rotated.y); }
    let sector = params.values.z;
    let local_angle = angle - floor(0.5 + angle / sector) * sector;
    let local = vec2<f32>(sin(local_angle), cos(local_angle)) * radial;
    let edge_y = radius * params.extra.x;
    let half_edge = radius * params.extra.y;
    let difference = vec2<f32>(max(abs(local.x) - half_edge, 0.0), local.y - edge_y);
    let unsigned_distance = length(difference);
    let distance = select(-unsigned_distance, unsigned_distance, local.y >= edge_y);
    let t = clamp(0.5 - distance / params.values.w, 0.0, 1.0);
    let coverage = t * t * (3.0 - 2.0 * t);
    pixels[id.y * params.dimensions.x + id.x] = mix(params.color2, params.color1, coverage);
}
