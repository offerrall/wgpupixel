@group(0) @binding(0) var<storage, read_write> pixels: array<vec4<f32>>;

fn perlin_hash(input: u32) -> u32 {
    var value = input;
    value = (value ^ (value >> 16u)) * 0x7feb352du;
    value = (value ^ (value >> 15u)) * 0x846ca68bu;
    return value ^ (value >> 16u);
}

fn perlin_gradient(cell: vec2<i32>, seed: u32, delta: vec2<f32>) -> f32 {
    let key = (bitcast<u32>(cell.x) * 0x1f123bb5u) ^ (bitcast<u32>(cell.y) * 0x5f356495u) ^ seed;
    switch (perlin_hash(key) & 7u) {
        case 0u: { return delta.x; }
        case 1u: { return -delta.x; }
        case 2u: { return delta.y; }
        case 3u: { return -delta.y; }
        case 4u: { return (delta.x + delta.y) * 0.7071067811865476; }
        case 5u: { return (delta.x - delta.y) * 0.7071067811865476; }
        case 6u: { return (-delta.x + delta.y) * 0.7071067811865476; }
        default: { return (-delta.x - delta.y) * 0.7071067811865476; }
    }
}

fn perlin_sample(position: vec2<f32>, seed: u32) -> f32 {
    let cell = vec2<i32>(floor(position));
    let delta = position - floor(position);
    let fade = delta * delta * delta * (delta * (delta * 6.0 - vec2<f32>(15.0)) + vec2<f32>(10.0));
    let aa = perlin_gradient(cell, seed, delta);
    let ba = perlin_gradient(cell + vec2<i32>(1, 0), seed, delta - vec2<f32>(1, 0));
    let ab = perlin_gradient(cell + vec2<i32>(0, 1), seed, delta - vec2<f32>(0, 1));
    let bb = perlin_gradient(cell + vec2<i32>(1, 1), seed, delta - vec2<f32>(1, 1));
    return mix(mix(aa, ba, fade.x), mix(ab, bb, fade.x), fade.y);
}

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) {
        return;
    }
    let position = (vec2<f32>(id.xy) + params.extra.xy) / params.values.x;
    var frequency = 1.0;
    var amplitude = 1.0;
    var total = 0.0;
    var normalization = 0.0;
    for (var octave = 0u; octave < params.reserved.y; octave++) {
        // All following amplitudes are zero too; neither sum can change.
        if (amplitude == 0.0) { break; }
        let seed = params.reserved.x + octave * 0x9e3779b9u;
        total += perlin_sample(position * frequency, seed) * amplitude;
        normalization += amplitude;
        if (octave + 1u < params.reserved.y) {
            frequency *= params.values.z;
            amplitude *= params.values.y;
        }
    }
    let coverage = clamp(0.5 + 0.5 * total / normalization, 0.0, 1.0);
    pixels[id.y * params.dimensions.x + id.x] = mix(params.color1, params.color2, coverage);
}
