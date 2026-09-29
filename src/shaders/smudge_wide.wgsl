//! include brush_round smudge_engine
//! coverage kernel
//! entry none
@group(0) @binding(0) var<storage, read_write> carried: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;
@group(0) @binding(7) var<storage, read> data: array<f32>;

fn carried_load(slot: u32) -> vec4<f32> { return carried[slot]; }
fn carried_store(slot: u32, value: vec4<f32>) { carried[slot] = value; }

// One dab per dispatch: each pixel belongs to one invocation.
fn canvas_load(index: u32) -> vec4<f32> { return destination[index]; }
fn canvas_store(index: u32, value: vec4<f32>) { destination[index] = value; }
