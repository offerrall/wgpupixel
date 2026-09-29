@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;

fn apply_operation(id: vec3<u32>) {
    if (id.x >= params.dimensions.z || id.y >= params.dimensions.w) {
        return;
    }
    var position = id.xy;
    if (params.reserved.x != 1u) {
        position.x = params.dimensions.x - 1u - position.x;
    }
    if (params.reserved.x != 0u) {
        position.y = params.dimensions.y - 1u - position.y;
    }
    destination[id.y * params.dimensions.z + id.x] =
        source[position.y * params.dimensions.x + position.x];
}
