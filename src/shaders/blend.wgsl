//! include blend_modes blend_pixel blend_add blend_soft_light blend_hard_light position
//! coverage offset
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) {
        return;
    }

    let x = destination_coordinate(id.x, params.offsets.x, params.dimensions.z);
    let y = destination_coordinate(id.y, params.offsets.y, params.dimensions.w);
    if (x >= params.dimensions.z || y >= params.dimensions.w) {
        return;
    }

    let source_index = id.y * params.dimensions.x + id.x;
    let destination_index = y * params.dimensions.z + x;
    let original = source[source_index];
    destination[destination_index] = blend_sample(original, destination[destination_index], params.values.x, params.reserved.x, id.xy, params.reserved.y);
}
