//! include brush_engine brush_mask brush_mask_write
//! coverage kernel
@group(0) @binding(0) var<storage, read> tip: array<u32>;
@group(0) @binding(1) var<storage, read_write> pixels: array<atomic<u32>>;
@group(0) @binding(7) var<storage, read> data: array<f32>;
