// Coherent pixel access for kernels running several dabs in one workgroup.
fn canvas_load(index: u32) -> vec4<f32> {
    return bitcast<vec4<f32>>(vec4<u32>(atomicLoad(&destination[4u * index]),
                                        atomicLoad(&destination[4u * index + 1u]),
                                        atomicLoad(&destination[4u * index + 2u]),
                                        atomicLoad(&destination[4u * index + 3u])));
}

fn canvas_store(index: u32, value: vec4<f32>) {
    let bits = bitcast<vec4<u32>>(value);
    for (var c = 0u; c < 4u; c += 1u) {
        atomicStore(&destination[4u * index + c], bits[c]);
    }
}
