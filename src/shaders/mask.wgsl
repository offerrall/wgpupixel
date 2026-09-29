//! include position
//! coverage offset
@group(0) @binding(0) var<storage, read> source: array<u32>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.x || id.y >= params.dimensions.y) { return; }
    let x = destination_coordinate(id.x, params.offsets.x, params.dimensions.z);
    let y = destination_coordinate(id.y, params.offsets.y, params.dimensions.w);
    if (x >= params.dimensions.z || y >= params.dimensions.w) { return; }
    let source_index = id.y * params.dimensions.x + id.x;
    let coverage = f32((source[source_index / 4u] >> ((source_index % 4u) * 8u)) & 255u) / 255.0;
    let index = y * params.dimensions.z + x;
    destination[index] *= coverage;
}
