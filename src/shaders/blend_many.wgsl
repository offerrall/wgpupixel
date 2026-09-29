//! include blend_modes blend_pixel blend_add blend_soft_light blend_hard_light
//! coverage kernel
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;
@group(0) @binding(4) var<storage, read> source1: array<vec4<f32>>;
@group(0) @binding(5) var<storage, read> source2: array<vec4<f32>>;
@group(0) @binding(6) var<storage, read> source3: array<vec4<f32>>;

fn source_size(layer: u32) -> vec2<u32> {
    switch layer {
        case 0u: { return params.dimensions.xy; }
        case 1u: { return bitcast<vec4<u32>>(params.color1).xy; }
        case 2u: { return bitcast<vec4<u32>>(params.color1).zw; }
        default: { return bitcast<vec4<u32>>(params.color2).xy; }
    }
}

fn source_position(layer: u32) -> vec2<i32> {
    switch layer {
        case 0u: { return params.offsets.xy; }
        case 1u: { return params.offsets.zw; }
        case 2u: { return bitcast<vec4<i32>>(params.extra).xy; }
        default: { return bitcast<vec4<i32>>(params.extra).zw; }
    }
}

fn source_pixel(layer: u32, index: u32) -> vec4<f32> {
    switch layer {
        case 0u: { return source[index]; }
        case 1u: { return source1[index]; }
        case 2u: { return source2[index]; }
        default: { return source3[index]; }
    }
}

fn source_coordinate(pixel: u32, offset: i32, extent: u32) -> u32 {
    if (offset >= 0) {
        if (pixel < u32(offset)) { return extent; }
        return pixel - u32(offset);
    }
    let distance = u32(-(offset + 1)) + 1u;
    if (distance >= extent || pixel >= extent - distance) { return extent; }
    return pixel + distance;
}

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.z || id.y >= params.dimensions.w) { return; }
    let index = id.y * params.dimensions.z + id.x;
    let coverage = operation_coverage(index);
    if (coverage == 0.0) { return; }
    var pixel = destination[index];
    let count = bitcast<u32>(params.color2.z);
    for (var layer = 0u; layer < count; layer++) {
        let size = source_size(layer);
        let position = source_position(layer);
        let x = source_coordinate(id.x, position.x, size.x);
        let y = source_coordinate(id.y, position.y, size.y);
        if (x >= size.x || y >= size.y) { continue; }
        let foreground = source_pixel(layer, y * size.x + x);
        let result = blend_sample(foreground, pixel, params.values[layer], params.reserved[layer], vec2<u32>(x, y), bitcast<u32>(params.extra2[layer]));
        pixel = select(mix(pixel, result, coverage), result, coverage == 1.0);
    }
    destination[index] = pixel;
}
