struct Parameters {
    dimensions: vec4<u32>,
    values: vec4<f32>,
    offsets: vec4<i32>,
    reserved: vec4<u32>,
    color1: vec4<f32>,
    color2: vec4<f32>,
    extra: vec4<f32>,
    extra2: vec4<f32>,
    dispatch: vec4<u32>,
};
@group(0) @binding(2) var<uniform> params: Parameters;
// Variable-length data channel: a kernel opts in by declaring
//   @group(0) @binding(7) var<storage, read> data: array<f32>; // or array<u32>/array<i32>
// It then sees exactly the 32-bit words its record staged with Operation::data;
// arrayLength(&data) is that count (1 for an empty payload, which reads as zero).
