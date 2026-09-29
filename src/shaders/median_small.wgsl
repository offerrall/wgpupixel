//! include filter_common
@group(0) @binding(0) var<storage, read> source: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> destination: array<vec4<f32>>;
fn median2(p: vec2<i32>) -> vec4<f32> {
    var p0 = sample_pixel(p + vec2<i32>(-2, -2));
    var p1 = sample_pixel(p + vec2<i32>(-1, -2));
    var p2 = sample_pixel(p + vec2<i32>(0, -2));
    var p3 = sample_pixel(p + vec2<i32>(1, -2));
    var p4 = sample_pixel(p + vec2<i32>(2, -2));
    var p5 = sample_pixel(p + vec2<i32>(-2, -1));
    var p6 = sample_pixel(p + vec2<i32>(-1, -1));
    var p7 = sample_pixel(p + vec2<i32>(0, -1));
    var p8 = sample_pixel(p + vec2<i32>(1, -1));
    var p9 = sample_pixel(p + vec2<i32>(2, -1));
    var p10 = sample_pixel(p + vec2<i32>(-2, 0));
    var p11 = sample_pixel(p + vec2<i32>(-1, 0));
    var p12 = sample_pixel(p + vec2<i32>(0, 0));
    var p13 = sample_pixel(p + vec2<i32>(1, 0));
    var p14 = sample_pixel(p + vec2<i32>(2, 0));
    var p15 = sample_pixel(p + vec2<i32>(-2, 1));
    var p16 = sample_pixel(p + vec2<i32>(-1, 1));
    var p17 = sample_pixel(p + vec2<i32>(0, 1));
    var p18 = sample_pixel(p + vec2<i32>(1, 1));
    var p19 = sample_pixel(p + vec2<i32>(2, 1));
    var p20 = sample_pixel(p + vec2<i32>(-2, 2));
    var p21 = sample_pixel(p + vec2<i32>(-1, 2));
    var p22 = sample_pixel(p + vec2<i32>(0, 2));
    var p23 = sample_pixel(p + vec2<i32>(1, 2));
    var p24 = sample_pixel(p + vec2<i32>(2, 2));
    var p25 = vec4<f32>(3.402823466e38);
    var p26 = vec4<f32>(3.402823466e38);
    var p27 = vec4<f32>(3.402823466e38);
    var p28 = vec4<f32>(3.402823466e38);
    var p29 = vec4<f32>(3.402823466e38);
    var p30 = vec4<f32>(3.402823466e38);
    var p31 = vec4<f32>(3.402823466e38);
    { let lo = min(p0, p1); p1 = max(p0, p1); p0 = lo; }
    { let lo = min(p3, p2); p2 = max(p3, p2); p3 = lo; }
    { let lo = min(p4, p5); p5 = max(p4, p5); p4 = lo; }
    { let lo = min(p7, p6); p6 = max(p7, p6); p7 = lo; }
    { let lo = min(p8, p9); p9 = max(p8, p9); p8 = lo; }
    { let lo = min(p11, p10); p10 = max(p11, p10); p11 = lo; }
    { let lo = min(p12, p13); p13 = max(p12, p13); p12 = lo; }
    { let lo = min(p15, p14); p14 = max(p15, p14); p15 = lo; }
    { let lo = min(p16, p17); p17 = max(p16, p17); p16 = lo; }
    { let lo = min(p19, p18); p18 = max(p19, p18); p19 = lo; }
    { let lo = min(p20, p21); p21 = max(p20, p21); p20 = lo; }
    { let lo = min(p23, p22); p22 = max(p23, p22); p23 = lo; }
    { let lo = min(p24, p25); p25 = max(p24, p25); p24 = lo; }
    { let lo = min(p27, p26); p26 = max(p27, p26); p27 = lo; }
    { let lo = min(p28, p29); p29 = max(p28, p29); p28 = lo; }
    { let lo = min(p31, p30); p30 = max(p31, p30); p31 = lo; }
    { let lo = min(p0, p2); p2 = max(p0, p2); p0 = lo; }
    { let lo = min(p1, p3); p3 = max(p1, p3); p1 = lo; }
    { let lo = min(p6, p4); p4 = max(p6, p4); p6 = lo; }
    { let lo = min(p7, p5); p5 = max(p7, p5); p7 = lo; }
    { let lo = min(p8, p10); p10 = max(p8, p10); p8 = lo; }
    { let lo = min(p9, p11); p11 = max(p9, p11); p9 = lo; }
    { let lo = min(p14, p12); p12 = max(p14, p12); p14 = lo; }
    { let lo = min(p15, p13); p13 = max(p15, p13); p15 = lo; }
    { let lo = min(p16, p18); p18 = max(p16, p18); p16 = lo; }
    { let lo = min(p17, p19); p19 = max(p17, p19); p17 = lo; }
    { let lo = min(p22, p20); p20 = max(p22, p20); p22 = lo; }
    { let lo = min(p23, p21); p21 = max(p23, p21); p23 = lo; }
    { let lo = min(p24, p26); p26 = max(p24, p26); p24 = lo; }
    { let lo = min(p25, p27); p27 = max(p25, p27); p25 = lo; }
    { let lo = min(p30, p28); p28 = max(p30, p28); p30 = lo; }
    { let lo = min(p31, p29); p29 = max(p31, p29); p31 = lo; }
    { let lo = min(p0, p1); p1 = max(p0, p1); p0 = lo; }
    { let lo = min(p2, p3); p3 = max(p2, p3); p2 = lo; }
    { let lo = min(p5, p4); p4 = max(p5, p4); p5 = lo; }
    { let lo = min(p7, p6); p6 = max(p7, p6); p7 = lo; }
    { let lo = min(p8, p9); p9 = max(p8, p9); p8 = lo; }
    { let lo = min(p10, p11); p11 = max(p10, p11); p10 = lo; }
    { let lo = min(p13, p12); p12 = max(p13, p12); p13 = lo; }
    { let lo = min(p15, p14); p14 = max(p15, p14); p15 = lo; }
    { let lo = min(p16, p17); p17 = max(p16, p17); p16 = lo; }
    { let lo = min(p18, p19); p19 = max(p18, p19); p18 = lo; }
    { let lo = min(p21, p20); p20 = max(p21, p20); p21 = lo; }
    { let lo = min(p23, p22); p22 = max(p23, p22); p23 = lo; }
    { let lo = min(p24, p25); p25 = max(p24, p25); p24 = lo; }
    { let lo = min(p26, p27); p27 = max(p26, p27); p26 = lo; }
    { let lo = min(p29, p28); p28 = max(p29, p28); p29 = lo; }
    { let lo = min(p31, p30); p30 = max(p31, p30); p31 = lo; }
    { let lo = min(p0, p4); p4 = max(p0, p4); p0 = lo; }
    { let lo = min(p1, p5); p5 = max(p1, p5); p1 = lo; }
    { let lo = min(p2, p6); p6 = max(p2, p6); p2 = lo; }
    { let lo = min(p3, p7); p7 = max(p3, p7); p3 = lo; }
    { let lo = min(p12, p8); p8 = max(p12, p8); p12 = lo; }
    { let lo = min(p13, p9); p9 = max(p13, p9); p13 = lo; }
    { let lo = min(p14, p10); p10 = max(p14, p10); p14 = lo; }
    { let lo = min(p15, p11); p11 = max(p15, p11); p15 = lo; }
    { let lo = min(p16, p20); p20 = max(p16, p20); p16 = lo; }
    { let lo = min(p17, p21); p21 = max(p17, p21); p17 = lo; }
    { let lo = min(p18, p22); p22 = max(p18, p22); p18 = lo; }
    { let lo = min(p19, p23); p23 = max(p19, p23); p19 = lo; }
    { let lo = min(p28, p24); p24 = max(p28, p24); p28 = lo; }
    { let lo = min(p29, p25); p25 = max(p29, p25); p29 = lo; }
    { let lo = min(p30, p26); p26 = max(p30, p26); p30 = lo; }
    { let lo = min(p31, p27); p27 = max(p31, p27); p31 = lo; }
    { let lo = min(p0, p2); p2 = max(p0, p2); p0 = lo; }
    { let lo = min(p1, p3); p3 = max(p1, p3); p1 = lo; }
    { let lo = min(p4, p6); p6 = max(p4, p6); p4 = lo; }
    { let lo = min(p5, p7); p7 = max(p5, p7); p5 = lo; }
    { let lo = min(p10, p8); p8 = max(p10, p8); p10 = lo; }
    { let lo = min(p11, p9); p9 = max(p11, p9); p11 = lo; }
    { let lo = min(p14, p12); p12 = max(p14, p12); p14 = lo; }
    { let lo = min(p15, p13); p13 = max(p15, p13); p15 = lo; }
    { let lo = min(p16, p18); p18 = max(p16, p18); p16 = lo; }
    { let lo = min(p17, p19); p19 = max(p17, p19); p17 = lo; }
    { let lo = min(p20, p22); p22 = max(p20, p22); p20 = lo; }
    { let lo = min(p21, p23); p23 = max(p21, p23); p21 = lo; }
    { let lo = min(p26, p24); p24 = max(p26, p24); p26 = lo; }
    { let lo = min(p27, p25); p25 = max(p27, p25); p27 = lo; }
    { let lo = min(p30, p28); p28 = max(p30, p28); p30 = lo; }
    { let lo = min(p31, p29); p29 = max(p31, p29); p31 = lo; }
    { let lo = min(p0, p1); p1 = max(p0, p1); p0 = lo; }
    { let lo = min(p2, p3); p3 = max(p2, p3); p2 = lo; }
    { let lo = min(p4, p5); p5 = max(p4, p5); p4 = lo; }
    { let lo = min(p6, p7); p7 = max(p6, p7); p6 = lo; }
    { let lo = min(p9, p8); p8 = max(p9, p8); p9 = lo; }
    { let lo = min(p11, p10); p10 = max(p11, p10); p11 = lo; }
    { let lo = min(p13, p12); p12 = max(p13, p12); p13 = lo; }
    { let lo = min(p15, p14); p14 = max(p15, p14); p15 = lo; }
    { let lo = min(p16, p17); p17 = max(p16, p17); p16 = lo; }
    { let lo = min(p18, p19); p19 = max(p18, p19); p18 = lo; }
    { let lo = min(p20, p21); p21 = max(p20, p21); p20 = lo; }
    { let lo = min(p22, p23); p23 = max(p22, p23); p22 = lo; }
    { let lo = min(p25, p24); p24 = max(p25, p24); p25 = lo; }
    { let lo = min(p27, p26); p26 = max(p27, p26); p27 = lo; }
    { let lo = min(p29, p28); p28 = max(p29, p28); p29 = lo; }
    { let lo = min(p31, p30); p30 = max(p31, p30); p31 = lo; }
    { let lo = min(p0, p8); p8 = max(p0, p8); p0 = lo; }
    { let lo = min(p1, p9); p9 = max(p1, p9); p1 = lo; }
    { let lo = min(p2, p10); p10 = max(p2, p10); p2 = lo; }
    { let lo = min(p3, p11); p11 = max(p3, p11); p3 = lo; }
    { let lo = min(p4, p12); p12 = max(p4, p12); p4 = lo; }
    { let lo = min(p5, p13); p13 = max(p5, p13); p5 = lo; }
    { let lo = min(p6, p14); p14 = max(p6, p14); p6 = lo; }
    { let lo = min(p7, p15); p15 = max(p7, p15); p7 = lo; }
    { let lo = min(p24, p16); p16 = max(p24, p16); p24 = lo; }
    { let lo = min(p25, p17); p17 = max(p25, p17); p25 = lo; }
    { let lo = min(p26, p18); p18 = max(p26, p18); p26 = lo; }
    { let lo = min(p27, p19); p19 = max(p27, p19); p27 = lo; }
    { let lo = min(p28, p20); p20 = max(p28, p20); p28 = lo; }
    { let lo = min(p29, p21); p21 = max(p29, p21); p29 = lo; }
    { let lo = min(p30, p22); p22 = max(p30, p22); p30 = lo; }
    { let lo = min(p31, p23); p23 = max(p31, p23); p31 = lo; }
    { let lo = min(p0, p4); p4 = max(p0, p4); p0 = lo; }
    { let lo = min(p1, p5); p5 = max(p1, p5); p1 = lo; }
    { let lo = min(p2, p6); p6 = max(p2, p6); p2 = lo; }
    { let lo = min(p3, p7); p7 = max(p3, p7); p3 = lo; }
    { let lo = min(p8, p12); p12 = max(p8, p12); p8 = lo; }
    { let lo = min(p9, p13); p13 = max(p9, p13); p9 = lo; }
    { let lo = min(p10, p14); p14 = max(p10, p14); p10 = lo; }
    { let lo = min(p11, p15); p15 = max(p11, p15); p11 = lo; }
    { let lo = min(p20, p16); p16 = max(p20, p16); p20 = lo; }
    { let lo = min(p21, p17); p17 = max(p21, p17); p21 = lo; }
    { let lo = min(p22, p18); p18 = max(p22, p18); p22 = lo; }
    { let lo = min(p23, p19); p19 = max(p23, p19); p23 = lo; }
    { let lo = min(p28, p24); p24 = max(p28, p24); p28 = lo; }
    { let lo = min(p29, p25); p25 = max(p29, p25); p29 = lo; }
    { let lo = min(p30, p26); p26 = max(p30, p26); p30 = lo; }
    { let lo = min(p31, p27); p27 = max(p31, p27); p31 = lo; }
    { let lo = min(p0, p2); p2 = max(p0, p2); p0 = lo; }
    { let lo = min(p1, p3); p3 = max(p1, p3); p1 = lo; }
    { let lo = min(p4, p6); p6 = max(p4, p6); p4 = lo; }
    { let lo = min(p5, p7); p7 = max(p5, p7); p5 = lo; }
    { let lo = min(p8, p10); p10 = max(p8, p10); p8 = lo; }
    { let lo = min(p9, p11); p11 = max(p9, p11); p9 = lo; }
    { let lo = min(p12, p14); p14 = max(p12, p14); p12 = lo; }
    { let lo = min(p13, p15); p15 = max(p13, p15); p13 = lo; }
    { let lo = min(p18, p16); p16 = max(p18, p16); p18 = lo; }
    { let lo = min(p19, p17); p17 = max(p19, p17); p19 = lo; }
    { let lo = min(p22, p20); p20 = max(p22, p20); p22 = lo; }
    { let lo = min(p23, p21); p21 = max(p23, p21); p23 = lo; }
    { let lo = min(p26, p24); p24 = max(p26, p24); p26 = lo; }
    { let lo = min(p27, p25); p25 = max(p27, p25); p27 = lo; }
    { let lo = min(p30, p28); p28 = max(p30, p28); p30 = lo; }
    { let lo = min(p31, p29); p29 = max(p31, p29); p31 = lo; }
    { let lo = min(p0, p1); p1 = max(p0, p1); p0 = lo; }
    { let lo = min(p2, p3); p3 = max(p2, p3); p2 = lo; }
    { let lo = min(p4, p5); p5 = max(p4, p5); p4 = lo; }
    { let lo = min(p6, p7); p7 = max(p6, p7); p6 = lo; }
    { let lo = min(p8, p9); p9 = max(p8, p9); p8 = lo; }
    { let lo = min(p10, p11); p11 = max(p10, p11); p10 = lo; }
    { let lo = min(p12, p13); p13 = max(p12, p13); p12 = lo; }
    { let lo = min(p14, p15); p15 = max(p14, p15); p14 = lo; }
    { let lo = min(p17, p16); p16 = max(p17, p16); p17 = lo; }
    { let lo = min(p19, p18); p18 = max(p19, p18); p19 = lo; }
    { let lo = min(p21, p20); p20 = max(p21, p20); p21 = lo; }
    { let lo = min(p23, p22); p22 = max(p23, p22); p23 = lo; }
    { let lo = min(p25, p24); p24 = max(p25, p24); p25 = lo; }
    { let lo = min(p27, p26); p26 = max(p27, p26); p27 = lo; }
    { let lo = min(p29, p28); p28 = max(p29, p28); p29 = lo; }
    { let lo = min(p31, p30); p30 = max(p31, p30); p31 = lo; }
    { let lo = min(p0, p16); p16 = max(p0, p16); p0 = lo; }
    { let lo = min(p1, p17); p17 = max(p1, p17); p1 = lo; }
    { let lo = min(p2, p18); p18 = max(p2, p18); p2 = lo; }
    { let lo = min(p3, p19); p19 = max(p3, p19); p3 = lo; }
    { let lo = min(p4, p20); p20 = max(p4, p20); p4 = lo; }
    { let lo = min(p5, p21); p21 = max(p5, p21); p5 = lo; }
    { let lo = min(p6, p22); p22 = max(p6, p22); p6 = lo; }
    { let lo = min(p7, p23); p23 = max(p7, p23); p7 = lo; }
    { let lo = min(p8, p24); p24 = max(p8, p24); p8 = lo; }
    { let lo = min(p9, p25); p25 = max(p9, p25); p9 = lo; }
    { let lo = min(p10, p26); p26 = max(p10, p26); p10 = lo; }
    { let lo = min(p11, p27); p27 = max(p11, p27); p11 = lo; }
    { let lo = min(p12, p28); p28 = max(p12, p28); p12 = lo; }
    { let lo = min(p13, p29); p29 = max(p13, p29); p13 = lo; }
    { let lo = min(p14, p30); p30 = max(p14, p30); p14 = lo; }
    { let lo = min(p15, p31); p31 = max(p15, p31); p15 = lo; }
    { let lo = min(p0, p8); p8 = max(p0, p8); p0 = lo; }
    { let lo = min(p1, p9); p9 = max(p1, p9); p1 = lo; }
    { let lo = min(p2, p10); p10 = max(p2, p10); p2 = lo; }
    { let lo = min(p3, p11); p11 = max(p3, p11); p3 = lo; }
    { let lo = min(p4, p12); p12 = max(p4, p12); p4 = lo; }
    { let lo = min(p5, p13); p13 = max(p5, p13); p5 = lo; }
    { let lo = min(p6, p14); p14 = max(p6, p14); p6 = lo; }
    { let lo = min(p7, p15); p15 = max(p7, p15); p7 = lo; }
    { let lo = min(p16, p24); p24 = max(p16, p24); p16 = lo; }
    { let lo = min(p17, p25); p25 = max(p17, p25); p17 = lo; }
    { let lo = min(p18, p26); p26 = max(p18, p26); p18 = lo; }
    { let lo = min(p19, p27); p27 = max(p19, p27); p19 = lo; }
    { let lo = min(p20, p28); p28 = max(p20, p28); p20 = lo; }
    { let lo = min(p21, p29); p29 = max(p21, p29); p21 = lo; }
    { let lo = min(p22, p30); p30 = max(p22, p30); p22 = lo; }
    { let lo = min(p23, p31); p31 = max(p23, p31); p23 = lo; }
    { let lo = min(p0, p4); p4 = max(p0, p4); p0 = lo; }
    { let lo = min(p1, p5); p5 = max(p1, p5); p1 = lo; }
    { let lo = min(p2, p6); p6 = max(p2, p6); p2 = lo; }
    { let lo = min(p3, p7); p7 = max(p3, p7); p3 = lo; }
    { let lo = min(p8, p12); p12 = max(p8, p12); p8 = lo; }
    { let lo = min(p9, p13); p13 = max(p9, p13); p9 = lo; }
    { let lo = min(p10, p14); p14 = max(p10, p14); p10 = lo; }
    { let lo = min(p11, p15); p15 = max(p11, p15); p11 = lo; }
    { let lo = min(p16, p20); p20 = max(p16, p20); p16 = lo; }
    { let lo = min(p17, p21); p21 = max(p17, p21); p17 = lo; }
    { let lo = min(p18, p22); p22 = max(p18, p22); p18 = lo; }
    { let lo = min(p19, p23); p23 = max(p19, p23); p19 = lo; }
    { let lo = min(p24, p28); p28 = max(p24, p28); p24 = lo; }
    { let lo = min(p25, p29); p29 = max(p25, p29); p25 = lo; }
    { let lo = min(p26, p30); p30 = max(p26, p30); p26 = lo; }
    { let lo = min(p27, p31); p31 = max(p27, p31); p27 = lo; }
    { let lo = min(p0, p2); p2 = max(p0, p2); p0 = lo; }
    { let lo = min(p1, p3); p3 = max(p1, p3); p1 = lo; }
    { let lo = min(p4, p6); p6 = max(p4, p6); p4 = lo; }
    { let lo = min(p5, p7); p7 = max(p5, p7); p5 = lo; }
    { let lo = min(p8, p10); p10 = max(p8, p10); p8 = lo; }
    { let lo = min(p9, p11); p11 = max(p9, p11); p9 = lo; }
    { let lo = min(p12, p14); p14 = max(p12, p14); p12 = lo; }
    { let lo = min(p13, p15); p15 = max(p13, p15); p13 = lo; }
    { let lo = min(p16, p18); p18 = max(p16, p18); p16 = lo; }
    { let lo = min(p17, p19); p19 = max(p17, p19); p17 = lo; }
    { let lo = min(p20, p22); p22 = max(p20, p22); p20 = lo; }
    { let lo = min(p21, p23); p23 = max(p21, p23); p21 = lo; }
    { let lo = min(p24, p26); p26 = max(p24, p26); p24 = lo; }
    { let lo = min(p25, p27); p27 = max(p25, p27); p25 = lo; }
    { let lo = min(p28, p30); p30 = max(p28, p30); p28 = lo; }
    { let lo = min(p29, p31); p31 = max(p29, p31); p29 = lo; }
    { let lo = min(p0, p1); p1 = max(p0, p1); p0 = lo; }
    { let lo = min(p2, p3); p3 = max(p2, p3); p2 = lo; }
    { let lo = min(p4, p5); p5 = max(p4, p5); p4 = lo; }
    { let lo = min(p6, p7); p7 = max(p6, p7); p6 = lo; }
    { let lo = min(p8, p9); p9 = max(p8, p9); p8 = lo; }
    { let lo = min(p10, p11); p11 = max(p10, p11); p10 = lo; }
    { let lo = min(p12, p13); p13 = max(p12, p13); p12 = lo; }
    { let lo = min(p14, p15); p15 = max(p14, p15); p14 = lo; }
    { let lo = min(p16, p17); p17 = max(p16, p17); p16 = lo; }
    { let lo = min(p18, p19); p19 = max(p18, p19); p18 = lo; }
    { let lo = min(p20, p21); p21 = max(p20, p21); p20 = lo; }
    { let lo = min(p22, p23); p23 = max(p22, p23); p22 = lo; }
    { let lo = min(p24, p25); p25 = max(p24, p25); p24 = lo; }
    { let lo = min(p26, p27); p27 = max(p26, p27); p26 = lo; }
    { let lo = min(p28, p29); p29 = max(p28, p29); p28 = lo; }
    { let lo = min(p30, p31); p31 = max(p30, p31); p30 = lo; }
    return p12;
}
fn median3(p: vec2<i32>) -> vec4<f32> {
    var p0 = sample_pixel(p + vec2<i32>(-3, -3));
    var p1 = sample_pixel(p + vec2<i32>(-2, -3));
    var p2 = sample_pixel(p + vec2<i32>(-1, -3));
    var p3 = sample_pixel(p + vec2<i32>(0, -3));
    var p4 = sample_pixel(p + vec2<i32>(1, -3));
    var p5 = sample_pixel(p + vec2<i32>(2, -3));
    var p6 = sample_pixel(p + vec2<i32>(3, -3));
    var p7 = sample_pixel(p + vec2<i32>(-3, -2));
    var p8 = sample_pixel(p + vec2<i32>(-2, -2));
    var p9 = sample_pixel(p + vec2<i32>(-1, -2));
    var p10 = sample_pixel(p + vec2<i32>(0, -2));
    var p11 = sample_pixel(p + vec2<i32>(1, -2));
    var p12 = sample_pixel(p + vec2<i32>(2, -2));
    var p13 = sample_pixel(p + vec2<i32>(3, -2));
    var p14 = sample_pixel(p + vec2<i32>(-3, -1));
    var p15 = sample_pixel(p + vec2<i32>(-2, -1));
    var p16 = sample_pixel(p + vec2<i32>(-1, -1));
    var p17 = sample_pixel(p + vec2<i32>(0, -1));
    var p18 = sample_pixel(p + vec2<i32>(1, -1));
    var p19 = sample_pixel(p + vec2<i32>(2, -1));
    var p20 = sample_pixel(p + vec2<i32>(3, -1));
    var p21 = sample_pixel(p + vec2<i32>(-3, 0));
    var p22 = sample_pixel(p + vec2<i32>(-2, 0));
    var p23 = sample_pixel(p + vec2<i32>(-1, 0));
    var p24 = sample_pixel(p + vec2<i32>(0, 0));
    var p25 = sample_pixel(p + vec2<i32>(1, 0));
    var p26 = sample_pixel(p + vec2<i32>(2, 0));
    var p27 = sample_pixel(p + vec2<i32>(3, 0));
    var p28 = sample_pixel(p + vec2<i32>(-3, 1));
    var p29 = sample_pixel(p + vec2<i32>(-2, 1));
    var p30 = sample_pixel(p + vec2<i32>(-1, 1));
    var p31 = sample_pixel(p + vec2<i32>(0, 1));
    var p32 = sample_pixel(p + vec2<i32>(1, 1));
    var p33 = sample_pixel(p + vec2<i32>(2, 1));
    var p34 = sample_pixel(p + vec2<i32>(3, 1));
    var p35 = sample_pixel(p + vec2<i32>(-3, 2));
    var p36 = sample_pixel(p + vec2<i32>(-2, 2));
    var p37 = sample_pixel(p + vec2<i32>(-1, 2));
    var p38 = sample_pixel(p + vec2<i32>(0, 2));
    var p39 = sample_pixel(p + vec2<i32>(1, 2));
    var p40 = sample_pixel(p + vec2<i32>(2, 2));
    var p41 = sample_pixel(p + vec2<i32>(3, 2));
    var p42 = sample_pixel(p + vec2<i32>(-3, 3));
    var p43 = sample_pixel(p + vec2<i32>(-2, 3));
    var p44 = sample_pixel(p + vec2<i32>(-1, 3));
    var p45 = sample_pixel(p + vec2<i32>(0, 3));
    var p46 = sample_pixel(p + vec2<i32>(1, 3));
    var p47 = sample_pixel(p + vec2<i32>(2, 3));
    var p48 = sample_pixel(p + vec2<i32>(3, 3));
    var p49 = vec4<f32>(3.402823466e38);
    var p50 = vec4<f32>(3.402823466e38);
    var p51 = vec4<f32>(3.402823466e38);
    var p52 = vec4<f32>(3.402823466e38);
    var p53 = vec4<f32>(3.402823466e38);
    var p54 = vec4<f32>(3.402823466e38);
    var p55 = vec4<f32>(3.402823466e38);
    var p56 = vec4<f32>(3.402823466e38);
    var p57 = vec4<f32>(3.402823466e38);
    var p58 = vec4<f32>(3.402823466e38);
    var p59 = vec4<f32>(3.402823466e38);
    var p60 = vec4<f32>(3.402823466e38);
    var p61 = vec4<f32>(3.402823466e38);
    var p62 = vec4<f32>(3.402823466e38);
    var p63 = vec4<f32>(3.402823466e38);
    { let lo = min(p0, p1); p1 = max(p0, p1); p0 = lo; }
    { let lo = min(p3, p2); p2 = max(p3, p2); p3 = lo; }
    { let lo = min(p4, p5); p5 = max(p4, p5); p4 = lo; }
    { let lo = min(p7, p6); p6 = max(p7, p6); p7 = lo; }
    { let lo = min(p8, p9); p9 = max(p8, p9); p8 = lo; }
    { let lo = min(p11, p10); p10 = max(p11, p10); p11 = lo; }
    { let lo = min(p12, p13); p13 = max(p12, p13); p12 = lo; }
    { let lo = min(p15, p14); p14 = max(p15, p14); p15 = lo; }
    { let lo = min(p16, p17); p17 = max(p16, p17); p16 = lo; }
    { let lo = min(p19, p18); p18 = max(p19, p18); p19 = lo; }
    { let lo = min(p20, p21); p21 = max(p20, p21); p20 = lo; }
    { let lo = min(p23, p22); p22 = max(p23, p22); p23 = lo; }
    { let lo = min(p24, p25); p25 = max(p24, p25); p24 = lo; }
    { let lo = min(p27, p26); p26 = max(p27, p26); p27 = lo; }
    { let lo = min(p28, p29); p29 = max(p28, p29); p28 = lo; }
    { let lo = min(p31, p30); p30 = max(p31, p30); p31 = lo; }
    { let lo = min(p32, p33); p33 = max(p32, p33); p32 = lo; }
    { let lo = min(p35, p34); p34 = max(p35, p34); p35 = lo; }
    { let lo = min(p36, p37); p37 = max(p36, p37); p36 = lo; }
    { let lo = min(p39, p38); p38 = max(p39, p38); p39 = lo; }
    { let lo = min(p40, p41); p41 = max(p40, p41); p40 = lo; }
    { let lo = min(p43, p42); p42 = max(p43, p42); p43 = lo; }
    { let lo = min(p44, p45); p45 = max(p44, p45); p44 = lo; }
    { let lo = min(p47, p46); p46 = max(p47, p46); p47 = lo; }
    { let lo = min(p48, p49); p49 = max(p48, p49); p48 = lo; }
    { let lo = min(p51, p50); p50 = max(p51, p50); p51 = lo; }
    { let lo = min(p52, p53); p53 = max(p52, p53); p52 = lo; }
    { let lo = min(p55, p54); p54 = max(p55, p54); p55 = lo; }
    { let lo = min(p56, p57); p57 = max(p56, p57); p56 = lo; }
    { let lo = min(p59, p58); p58 = max(p59, p58); p59 = lo; }
    { let lo = min(p60, p61); p61 = max(p60, p61); p60 = lo; }
    { let lo = min(p63, p62); p62 = max(p63, p62); p63 = lo; }
    { let lo = min(p0, p2); p2 = max(p0, p2); p0 = lo; }
    { let lo = min(p1, p3); p3 = max(p1, p3); p1 = lo; }
    { let lo = min(p6, p4); p4 = max(p6, p4); p6 = lo; }
    { let lo = min(p7, p5); p5 = max(p7, p5); p7 = lo; }
    { let lo = min(p8, p10); p10 = max(p8, p10); p8 = lo; }
    { let lo = min(p9, p11); p11 = max(p9, p11); p9 = lo; }
    { let lo = min(p14, p12); p12 = max(p14, p12); p14 = lo; }
    { let lo = min(p15, p13); p13 = max(p15, p13); p15 = lo; }
    { let lo = min(p16, p18); p18 = max(p16, p18); p16 = lo; }
    { let lo = min(p17, p19); p19 = max(p17, p19); p17 = lo; }
    { let lo = min(p22, p20); p20 = max(p22, p20); p22 = lo; }
    { let lo = min(p23, p21); p21 = max(p23, p21); p23 = lo; }
    { let lo = min(p24, p26); p26 = max(p24, p26); p24 = lo; }
    { let lo = min(p25, p27); p27 = max(p25, p27); p25 = lo; }
    { let lo = min(p30, p28); p28 = max(p30, p28); p30 = lo; }
    { let lo = min(p31, p29); p29 = max(p31, p29); p31 = lo; }
    { let lo = min(p32, p34); p34 = max(p32, p34); p32 = lo; }
    { let lo = min(p33, p35); p35 = max(p33, p35); p33 = lo; }
    { let lo = min(p38, p36); p36 = max(p38, p36); p38 = lo; }
    { let lo = min(p39, p37); p37 = max(p39, p37); p39 = lo; }
    { let lo = min(p40, p42); p42 = max(p40, p42); p40 = lo; }
    { let lo = min(p41, p43); p43 = max(p41, p43); p41 = lo; }
    { let lo = min(p46, p44); p44 = max(p46, p44); p46 = lo; }
    { let lo = min(p47, p45); p45 = max(p47, p45); p47 = lo; }
    { let lo = min(p48, p50); p50 = max(p48, p50); p48 = lo; }
    { let lo = min(p49, p51); p51 = max(p49, p51); p49 = lo; }
    { let lo = min(p54, p52); p52 = max(p54, p52); p54 = lo; }
    { let lo = min(p55, p53); p53 = max(p55, p53); p55 = lo; }
    { let lo = min(p56, p58); p58 = max(p56, p58); p56 = lo; }
    { let lo = min(p57, p59); p59 = max(p57, p59); p57 = lo; }
    { let lo = min(p62, p60); p60 = max(p62, p60); p62 = lo; }
    { let lo = min(p63, p61); p61 = max(p63, p61); p63 = lo; }
    { let lo = min(p0, p1); p1 = max(p0, p1); p0 = lo; }
    { let lo = min(p2, p3); p3 = max(p2, p3); p2 = lo; }
    { let lo = min(p5, p4); p4 = max(p5, p4); p5 = lo; }
    { let lo = min(p7, p6); p6 = max(p7, p6); p7 = lo; }
    { let lo = min(p8, p9); p9 = max(p8, p9); p8 = lo; }
    { let lo = min(p10, p11); p11 = max(p10, p11); p10 = lo; }
    { let lo = min(p13, p12); p12 = max(p13, p12); p13 = lo; }
    { let lo = min(p15, p14); p14 = max(p15, p14); p15 = lo; }
    { let lo = min(p16, p17); p17 = max(p16, p17); p16 = lo; }
    { let lo = min(p18, p19); p19 = max(p18, p19); p18 = lo; }
    { let lo = min(p21, p20); p20 = max(p21, p20); p21 = lo; }
    { let lo = min(p23, p22); p22 = max(p23, p22); p23 = lo; }
    { let lo = min(p24, p25); p25 = max(p24, p25); p24 = lo; }
    { let lo = min(p26, p27); p27 = max(p26, p27); p26 = lo; }
    { let lo = min(p29, p28); p28 = max(p29, p28); p29 = lo; }
    { let lo = min(p31, p30); p30 = max(p31, p30); p31 = lo; }
    { let lo = min(p32, p33); p33 = max(p32, p33); p32 = lo; }
    { let lo = min(p34, p35); p35 = max(p34, p35); p34 = lo; }
    { let lo = min(p37, p36); p36 = max(p37, p36); p37 = lo; }
    { let lo = min(p39, p38); p38 = max(p39, p38); p39 = lo; }
    { let lo = min(p40, p41); p41 = max(p40, p41); p40 = lo; }
    { let lo = min(p42, p43); p43 = max(p42, p43); p42 = lo; }
    { let lo = min(p45, p44); p44 = max(p45, p44); p45 = lo; }
    { let lo = min(p47, p46); p46 = max(p47, p46); p47 = lo; }
    { let lo = min(p48, p49); p49 = max(p48, p49); p48 = lo; }
    { let lo = min(p50, p51); p51 = max(p50, p51); p50 = lo; }
    { let lo = min(p53, p52); p52 = max(p53, p52); p53 = lo; }
    { let lo = min(p55, p54); p54 = max(p55, p54); p55 = lo; }
    { let lo = min(p56, p57); p57 = max(p56, p57); p56 = lo; }
    { let lo = min(p58, p59); p59 = max(p58, p59); p58 = lo; }
    { let lo = min(p61, p60); p60 = max(p61, p60); p61 = lo; }
    { let lo = min(p63, p62); p62 = max(p63, p62); p63 = lo; }
    { let lo = min(p0, p4); p4 = max(p0, p4); p0 = lo; }
    { let lo = min(p1, p5); p5 = max(p1, p5); p1 = lo; }
    { let lo = min(p2, p6); p6 = max(p2, p6); p2 = lo; }
    { let lo = min(p3, p7); p7 = max(p3, p7); p3 = lo; }
    { let lo = min(p12, p8); p8 = max(p12, p8); p12 = lo; }
    { let lo = min(p13, p9); p9 = max(p13, p9); p13 = lo; }
    { let lo = min(p14, p10); p10 = max(p14, p10); p14 = lo; }
    { let lo = min(p15, p11); p11 = max(p15, p11); p15 = lo; }
    { let lo = min(p16, p20); p20 = max(p16, p20); p16 = lo; }
    { let lo = min(p17, p21); p21 = max(p17, p21); p17 = lo; }
    { let lo = min(p18, p22); p22 = max(p18, p22); p18 = lo; }
    { let lo = min(p19, p23); p23 = max(p19, p23); p19 = lo; }
    { let lo = min(p28, p24); p24 = max(p28, p24); p28 = lo; }
    { let lo = min(p29, p25); p25 = max(p29, p25); p29 = lo; }
    { let lo = min(p30, p26); p26 = max(p30, p26); p30 = lo; }
    { let lo = min(p31, p27); p27 = max(p31, p27); p31 = lo; }
    { let lo = min(p32, p36); p36 = max(p32, p36); p32 = lo; }
    { let lo = min(p33, p37); p37 = max(p33, p37); p33 = lo; }
    { let lo = min(p34, p38); p38 = max(p34, p38); p34 = lo; }
    { let lo = min(p35, p39); p39 = max(p35, p39); p35 = lo; }
    { let lo = min(p44, p40); p40 = max(p44, p40); p44 = lo; }
    { let lo = min(p45, p41); p41 = max(p45, p41); p45 = lo; }
    { let lo = min(p46, p42); p42 = max(p46, p42); p46 = lo; }
    { let lo = min(p47, p43); p43 = max(p47, p43); p47 = lo; }
    { let lo = min(p48, p52); p52 = max(p48, p52); p48 = lo; }
    { let lo = min(p49, p53); p53 = max(p49, p53); p49 = lo; }
    { let lo = min(p50, p54); p54 = max(p50, p54); p50 = lo; }
    { let lo = min(p51, p55); p55 = max(p51, p55); p51 = lo; }
    { let lo = min(p60, p56); p56 = max(p60, p56); p60 = lo; }
    { let lo = min(p61, p57); p57 = max(p61, p57); p61 = lo; }
    { let lo = min(p62, p58); p58 = max(p62, p58); p62 = lo; }
    { let lo = min(p63, p59); p59 = max(p63, p59); p63 = lo; }
    { let lo = min(p0, p2); p2 = max(p0, p2); p0 = lo; }
    { let lo = min(p1, p3); p3 = max(p1, p3); p1 = lo; }
    { let lo = min(p4, p6); p6 = max(p4, p6); p4 = lo; }
    { let lo = min(p5, p7); p7 = max(p5, p7); p5 = lo; }
    { let lo = min(p10, p8); p8 = max(p10, p8); p10 = lo; }
    { let lo = min(p11, p9); p9 = max(p11, p9); p11 = lo; }
    { let lo = min(p14, p12); p12 = max(p14, p12); p14 = lo; }
    { let lo = min(p15, p13); p13 = max(p15, p13); p15 = lo; }
    { let lo = min(p16, p18); p18 = max(p16, p18); p16 = lo; }
    { let lo = min(p17, p19); p19 = max(p17, p19); p17 = lo; }
    { let lo = min(p20, p22); p22 = max(p20, p22); p20 = lo; }
    { let lo = min(p21, p23); p23 = max(p21, p23); p21 = lo; }
    { let lo = min(p26, p24); p24 = max(p26, p24); p26 = lo; }
    { let lo = min(p27, p25); p25 = max(p27, p25); p27 = lo; }
    { let lo = min(p30, p28); p28 = max(p30, p28); p30 = lo; }
    { let lo = min(p31, p29); p29 = max(p31, p29); p31 = lo; }
    { let lo = min(p32, p34); p34 = max(p32, p34); p32 = lo; }
    { let lo = min(p33, p35); p35 = max(p33, p35); p33 = lo; }
    { let lo = min(p36, p38); p38 = max(p36, p38); p36 = lo; }
    { let lo = min(p37, p39); p39 = max(p37, p39); p37 = lo; }
    { let lo = min(p42, p40); p40 = max(p42, p40); p42 = lo; }
    { let lo = min(p43, p41); p41 = max(p43, p41); p43 = lo; }
    { let lo = min(p46, p44); p44 = max(p46, p44); p46 = lo; }
    { let lo = min(p47, p45); p45 = max(p47, p45); p47 = lo; }
    { let lo = min(p48, p50); p50 = max(p48, p50); p48 = lo; }
    { let lo = min(p49, p51); p51 = max(p49, p51); p49 = lo; }
    { let lo = min(p52, p54); p54 = max(p52, p54); p52 = lo; }
    { let lo = min(p53, p55); p55 = max(p53, p55); p53 = lo; }
    { let lo = min(p58, p56); p56 = max(p58, p56); p58 = lo; }
    { let lo = min(p59, p57); p57 = max(p59, p57); p59 = lo; }
    { let lo = min(p62, p60); p60 = max(p62, p60); p62 = lo; }
    { let lo = min(p63, p61); p61 = max(p63, p61); p63 = lo; }
    { let lo = min(p0, p1); p1 = max(p0, p1); p0 = lo; }
    { let lo = min(p2, p3); p3 = max(p2, p3); p2 = lo; }
    { let lo = min(p4, p5); p5 = max(p4, p5); p4 = lo; }
    { let lo = min(p6, p7); p7 = max(p6, p7); p6 = lo; }
    { let lo = min(p9, p8); p8 = max(p9, p8); p9 = lo; }
    { let lo = min(p11, p10); p10 = max(p11, p10); p11 = lo; }
    { let lo = min(p13, p12); p12 = max(p13, p12); p13 = lo; }
    { let lo = min(p15, p14); p14 = max(p15, p14); p15 = lo; }
    { let lo = min(p16, p17); p17 = max(p16, p17); p16 = lo; }
    { let lo = min(p18, p19); p19 = max(p18, p19); p18 = lo; }
    { let lo = min(p20, p21); p21 = max(p20, p21); p20 = lo; }
    { let lo = min(p22, p23); p23 = max(p22, p23); p22 = lo; }
    { let lo = min(p25, p24); p24 = max(p25, p24); p25 = lo; }
    { let lo = min(p27, p26); p26 = max(p27, p26); p27 = lo; }
    { let lo = min(p29, p28); p28 = max(p29, p28); p29 = lo; }
    { let lo = min(p31, p30); p30 = max(p31, p30); p31 = lo; }
    { let lo = min(p32, p33); p33 = max(p32, p33); p32 = lo; }
    { let lo = min(p34, p35); p35 = max(p34, p35); p34 = lo; }
    { let lo = min(p36, p37); p37 = max(p36, p37); p36 = lo; }
    { let lo = min(p38, p39); p39 = max(p38, p39); p38 = lo; }
    { let lo = min(p41, p40); p40 = max(p41, p40); p41 = lo; }
    { let lo = min(p43, p42); p42 = max(p43, p42); p43 = lo; }
    { let lo = min(p45, p44); p44 = max(p45, p44); p45 = lo; }
    { let lo = min(p47, p46); p46 = max(p47, p46); p47 = lo; }
    { let lo = min(p48, p49); p49 = max(p48, p49); p48 = lo; }
    { let lo = min(p50, p51); p51 = max(p50, p51); p50 = lo; }
    { let lo = min(p52, p53); p53 = max(p52, p53); p52 = lo; }
    { let lo = min(p54, p55); p55 = max(p54, p55); p54 = lo; }
    { let lo = min(p57, p56); p56 = max(p57, p56); p57 = lo; }
    { let lo = min(p59, p58); p58 = max(p59, p58); p59 = lo; }
    { let lo = min(p61, p60); p60 = max(p61, p60); p61 = lo; }
    { let lo = min(p63, p62); p62 = max(p63, p62); p63 = lo; }
    { let lo = min(p0, p8); p8 = max(p0, p8); p0 = lo; }
    { let lo = min(p1, p9); p9 = max(p1, p9); p1 = lo; }
    { let lo = min(p2, p10); p10 = max(p2, p10); p2 = lo; }
    { let lo = min(p3, p11); p11 = max(p3, p11); p3 = lo; }
    { let lo = min(p4, p12); p12 = max(p4, p12); p4 = lo; }
    { let lo = min(p5, p13); p13 = max(p5, p13); p5 = lo; }
    { let lo = min(p6, p14); p14 = max(p6, p14); p6 = lo; }
    { let lo = min(p7, p15); p15 = max(p7, p15); p7 = lo; }
    { let lo = min(p24, p16); p16 = max(p24, p16); p24 = lo; }
    { let lo = min(p25, p17); p17 = max(p25, p17); p25 = lo; }
    { let lo = min(p26, p18); p18 = max(p26, p18); p26 = lo; }
    { let lo = min(p27, p19); p19 = max(p27, p19); p27 = lo; }
    { let lo = min(p28, p20); p20 = max(p28, p20); p28 = lo; }
    { let lo = min(p29, p21); p21 = max(p29, p21); p29 = lo; }
    { let lo = min(p30, p22); p22 = max(p30, p22); p30 = lo; }
    { let lo = min(p31, p23); p23 = max(p31, p23); p31 = lo; }
    { let lo = min(p32, p40); p40 = max(p32, p40); p32 = lo; }
    { let lo = min(p33, p41); p41 = max(p33, p41); p33 = lo; }
    { let lo = min(p34, p42); p42 = max(p34, p42); p34 = lo; }
    { let lo = min(p35, p43); p43 = max(p35, p43); p35 = lo; }
    { let lo = min(p36, p44); p44 = max(p36, p44); p36 = lo; }
    { let lo = min(p37, p45); p45 = max(p37, p45); p37 = lo; }
    { let lo = min(p38, p46); p46 = max(p38, p46); p38 = lo; }
    { let lo = min(p39, p47); p47 = max(p39, p47); p39 = lo; }
    { let lo = min(p56, p48); p48 = max(p56, p48); p56 = lo; }
    { let lo = min(p57, p49); p49 = max(p57, p49); p57 = lo; }
    { let lo = min(p58, p50); p50 = max(p58, p50); p58 = lo; }
    { let lo = min(p59, p51); p51 = max(p59, p51); p59 = lo; }
    { let lo = min(p60, p52); p52 = max(p60, p52); p60 = lo; }
    { let lo = min(p61, p53); p53 = max(p61, p53); p61 = lo; }
    { let lo = min(p62, p54); p54 = max(p62, p54); p62 = lo; }
    { let lo = min(p63, p55); p55 = max(p63, p55); p63 = lo; }
    { let lo = min(p0, p4); p4 = max(p0, p4); p0 = lo; }
    { let lo = min(p1, p5); p5 = max(p1, p5); p1 = lo; }
    { let lo = min(p2, p6); p6 = max(p2, p6); p2 = lo; }
    { let lo = min(p3, p7); p7 = max(p3, p7); p3 = lo; }
    { let lo = min(p8, p12); p12 = max(p8, p12); p8 = lo; }
    { let lo = min(p9, p13); p13 = max(p9, p13); p9 = lo; }
    { let lo = min(p10, p14); p14 = max(p10, p14); p10 = lo; }
    { let lo = min(p11, p15); p15 = max(p11, p15); p11 = lo; }
    { let lo = min(p20, p16); p16 = max(p20, p16); p20 = lo; }
    { let lo = min(p21, p17); p17 = max(p21, p17); p21 = lo; }
    { let lo = min(p22, p18); p18 = max(p22, p18); p22 = lo; }
    { let lo = min(p23, p19); p19 = max(p23, p19); p23 = lo; }
    { let lo = min(p28, p24); p24 = max(p28, p24); p28 = lo; }
    { let lo = min(p29, p25); p25 = max(p29, p25); p29 = lo; }
    { let lo = min(p30, p26); p26 = max(p30, p26); p30 = lo; }
    { let lo = min(p31, p27); p27 = max(p31, p27); p31 = lo; }
    { let lo = min(p32, p36); p36 = max(p32, p36); p32 = lo; }
    { let lo = min(p33, p37); p37 = max(p33, p37); p33 = lo; }
    { let lo = min(p34, p38); p38 = max(p34, p38); p34 = lo; }
    { let lo = min(p35, p39); p39 = max(p35, p39); p35 = lo; }
    { let lo = min(p40, p44); p44 = max(p40, p44); p40 = lo; }
    { let lo = min(p41, p45); p45 = max(p41, p45); p41 = lo; }
    { let lo = min(p42, p46); p46 = max(p42, p46); p42 = lo; }
    { let lo = min(p43, p47); p47 = max(p43, p47); p43 = lo; }
    { let lo = min(p52, p48); p48 = max(p52, p48); p52 = lo; }
    { let lo = min(p53, p49); p49 = max(p53, p49); p53 = lo; }
    { let lo = min(p54, p50); p50 = max(p54, p50); p54 = lo; }
    { let lo = min(p55, p51); p51 = max(p55, p51); p55 = lo; }
    { let lo = min(p60, p56); p56 = max(p60, p56); p60 = lo; }
    { let lo = min(p61, p57); p57 = max(p61, p57); p61 = lo; }
    { let lo = min(p62, p58); p58 = max(p62, p58); p62 = lo; }
    { let lo = min(p63, p59); p59 = max(p63, p59); p63 = lo; }
    { let lo = min(p0, p2); p2 = max(p0, p2); p0 = lo; }
    { let lo = min(p1, p3); p3 = max(p1, p3); p1 = lo; }
    { let lo = min(p4, p6); p6 = max(p4, p6); p4 = lo; }
    { let lo = min(p5, p7); p7 = max(p5, p7); p5 = lo; }
    { let lo = min(p8, p10); p10 = max(p8, p10); p8 = lo; }
    { let lo = min(p9, p11); p11 = max(p9, p11); p9 = lo; }
    { let lo = min(p12, p14); p14 = max(p12, p14); p12 = lo; }
    { let lo = min(p13, p15); p15 = max(p13, p15); p13 = lo; }
    { let lo = min(p18, p16); p16 = max(p18, p16); p18 = lo; }
    { let lo = min(p19, p17); p17 = max(p19, p17); p19 = lo; }
    { let lo = min(p22, p20); p20 = max(p22, p20); p22 = lo; }
    { let lo = min(p23, p21); p21 = max(p23, p21); p23 = lo; }
    { let lo = min(p26, p24); p24 = max(p26, p24); p26 = lo; }
    { let lo = min(p27, p25); p25 = max(p27, p25); p27 = lo; }
    { let lo = min(p30, p28); p28 = max(p30, p28); p30 = lo; }
    { let lo = min(p31, p29); p29 = max(p31, p29); p31 = lo; }
    { let lo = min(p32, p34); p34 = max(p32, p34); p32 = lo; }
    { let lo = min(p33, p35); p35 = max(p33, p35); p33 = lo; }
    { let lo = min(p36, p38); p38 = max(p36, p38); p36 = lo; }
    { let lo = min(p37, p39); p39 = max(p37, p39); p37 = lo; }
    { let lo = min(p40, p42); p42 = max(p40, p42); p40 = lo; }
    { let lo = min(p41, p43); p43 = max(p41, p43); p41 = lo; }
    { let lo = min(p44, p46); p46 = max(p44, p46); p44 = lo; }
    { let lo = min(p45, p47); p47 = max(p45, p47); p45 = lo; }
    { let lo = min(p50, p48); p48 = max(p50, p48); p50 = lo; }
    { let lo = min(p51, p49); p49 = max(p51, p49); p51 = lo; }
    { let lo = min(p54, p52); p52 = max(p54, p52); p54 = lo; }
    { let lo = min(p55, p53); p53 = max(p55, p53); p55 = lo; }
    { let lo = min(p58, p56); p56 = max(p58, p56); p58 = lo; }
    { let lo = min(p59, p57); p57 = max(p59, p57); p59 = lo; }
    { let lo = min(p62, p60); p60 = max(p62, p60); p62 = lo; }
    { let lo = min(p63, p61); p61 = max(p63, p61); p63 = lo; }
    { let lo = min(p0, p1); p1 = max(p0, p1); p0 = lo; }
    { let lo = min(p2, p3); p3 = max(p2, p3); p2 = lo; }
    { let lo = min(p4, p5); p5 = max(p4, p5); p4 = lo; }
    { let lo = min(p6, p7); p7 = max(p6, p7); p6 = lo; }
    { let lo = min(p8, p9); p9 = max(p8, p9); p8 = lo; }
    { let lo = min(p10, p11); p11 = max(p10, p11); p10 = lo; }
    { let lo = min(p12, p13); p13 = max(p12, p13); p12 = lo; }
    { let lo = min(p14, p15); p15 = max(p14, p15); p14 = lo; }
    { let lo = min(p17, p16); p16 = max(p17, p16); p17 = lo; }
    { let lo = min(p19, p18); p18 = max(p19, p18); p19 = lo; }
    { let lo = min(p21, p20); p20 = max(p21, p20); p21 = lo; }
    { let lo = min(p23, p22); p22 = max(p23, p22); p23 = lo; }
    { let lo = min(p25, p24); p24 = max(p25, p24); p25 = lo; }
    { let lo = min(p27, p26); p26 = max(p27, p26); p27 = lo; }
    { let lo = min(p29, p28); p28 = max(p29, p28); p29 = lo; }
    { let lo = min(p31, p30); p30 = max(p31, p30); p31 = lo; }
    { let lo = min(p32, p33); p33 = max(p32, p33); p32 = lo; }
    { let lo = min(p34, p35); p35 = max(p34, p35); p34 = lo; }
    { let lo = min(p36, p37); p37 = max(p36, p37); p36 = lo; }
    { let lo = min(p38, p39); p39 = max(p38, p39); p38 = lo; }
    { let lo = min(p40, p41); p41 = max(p40, p41); p40 = lo; }
    { let lo = min(p42, p43); p43 = max(p42, p43); p42 = lo; }
    { let lo = min(p44, p45); p45 = max(p44, p45); p44 = lo; }
    { let lo = min(p46, p47); p47 = max(p46, p47); p46 = lo; }
    { let lo = min(p49, p48); p48 = max(p49, p48); p49 = lo; }
    { let lo = min(p51, p50); p50 = max(p51, p50); p51 = lo; }
    { let lo = min(p53, p52); p52 = max(p53, p52); p53 = lo; }
    { let lo = min(p55, p54); p54 = max(p55, p54); p55 = lo; }
    { let lo = min(p57, p56); p56 = max(p57, p56); p57 = lo; }
    { let lo = min(p59, p58); p58 = max(p59, p58); p59 = lo; }
    { let lo = min(p61, p60); p60 = max(p61, p60); p61 = lo; }
    { let lo = min(p63, p62); p62 = max(p63, p62); p63 = lo; }
    { let lo = min(p0, p16); p16 = max(p0, p16); p0 = lo; }
    { let lo = min(p1, p17); p17 = max(p1, p17); p1 = lo; }
    { let lo = min(p2, p18); p18 = max(p2, p18); p2 = lo; }
    { let lo = min(p3, p19); p19 = max(p3, p19); p3 = lo; }
    { let lo = min(p4, p20); p20 = max(p4, p20); p4 = lo; }
    { let lo = min(p5, p21); p21 = max(p5, p21); p5 = lo; }
    { let lo = min(p6, p22); p22 = max(p6, p22); p6 = lo; }
    { let lo = min(p7, p23); p23 = max(p7, p23); p7 = lo; }
    { let lo = min(p8, p24); p24 = max(p8, p24); p8 = lo; }
    { let lo = min(p9, p25); p25 = max(p9, p25); p9 = lo; }
    { let lo = min(p10, p26); p26 = max(p10, p26); p10 = lo; }
    { let lo = min(p11, p27); p27 = max(p11, p27); p11 = lo; }
    { let lo = min(p12, p28); p28 = max(p12, p28); p12 = lo; }
    { let lo = min(p13, p29); p29 = max(p13, p29); p13 = lo; }
    { let lo = min(p14, p30); p30 = max(p14, p30); p14 = lo; }
    { let lo = min(p15, p31); p31 = max(p15, p31); p15 = lo; }
    { let lo = min(p48, p32); p32 = max(p48, p32); p48 = lo; }
    { let lo = min(p49, p33); p33 = max(p49, p33); p49 = lo; }
    { let lo = min(p50, p34); p34 = max(p50, p34); p50 = lo; }
    { let lo = min(p51, p35); p35 = max(p51, p35); p51 = lo; }
    { let lo = min(p52, p36); p36 = max(p52, p36); p52 = lo; }
    { let lo = min(p53, p37); p37 = max(p53, p37); p53 = lo; }
    { let lo = min(p54, p38); p38 = max(p54, p38); p54 = lo; }
    { let lo = min(p55, p39); p39 = max(p55, p39); p55 = lo; }
    { let lo = min(p56, p40); p40 = max(p56, p40); p56 = lo; }
    { let lo = min(p57, p41); p41 = max(p57, p41); p57 = lo; }
    { let lo = min(p58, p42); p42 = max(p58, p42); p58 = lo; }
    { let lo = min(p59, p43); p43 = max(p59, p43); p59 = lo; }
    { let lo = min(p60, p44); p44 = max(p60, p44); p60 = lo; }
    { let lo = min(p61, p45); p45 = max(p61, p45); p61 = lo; }
    { let lo = min(p62, p46); p46 = max(p62, p46); p62 = lo; }
    { let lo = min(p63, p47); p47 = max(p63, p47); p63 = lo; }
    { let lo = min(p0, p8); p8 = max(p0, p8); p0 = lo; }
    { let lo = min(p1, p9); p9 = max(p1, p9); p1 = lo; }
    { let lo = min(p2, p10); p10 = max(p2, p10); p2 = lo; }
    { let lo = min(p3, p11); p11 = max(p3, p11); p3 = lo; }
    { let lo = min(p4, p12); p12 = max(p4, p12); p4 = lo; }
    { let lo = min(p5, p13); p13 = max(p5, p13); p5 = lo; }
    { let lo = min(p6, p14); p14 = max(p6, p14); p6 = lo; }
    { let lo = min(p7, p15); p15 = max(p7, p15); p7 = lo; }
    { let lo = min(p16, p24); p24 = max(p16, p24); p16 = lo; }
    { let lo = min(p17, p25); p25 = max(p17, p25); p17 = lo; }
    { let lo = min(p18, p26); p26 = max(p18, p26); p18 = lo; }
    { let lo = min(p19, p27); p27 = max(p19, p27); p19 = lo; }
    { let lo = min(p20, p28); p28 = max(p20, p28); p20 = lo; }
    { let lo = min(p21, p29); p29 = max(p21, p29); p21 = lo; }
    { let lo = min(p22, p30); p30 = max(p22, p30); p22 = lo; }
    { let lo = min(p23, p31); p31 = max(p23, p31); p23 = lo; }
    { let lo = min(p40, p32); p32 = max(p40, p32); p40 = lo; }
    { let lo = min(p41, p33); p33 = max(p41, p33); p41 = lo; }
    { let lo = min(p42, p34); p34 = max(p42, p34); p42 = lo; }
    { let lo = min(p43, p35); p35 = max(p43, p35); p43 = lo; }
    { let lo = min(p44, p36); p36 = max(p44, p36); p44 = lo; }
    { let lo = min(p45, p37); p37 = max(p45, p37); p45 = lo; }
    { let lo = min(p46, p38); p38 = max(p46, p38); p46 = lo; }
    { let lo = min(p47, p39); p39 = max(p47, p39); p47 = lo; }
    { let lo = min(p56, p48); p48 = max(p56, p48); p56 = lo; }
    { let lo = min(p57, p49); p49 = max(p57, p49); p57 = lo; }
    { let lo = min(p58, p50); p50 = max(p58, p50); p58 = lo; }
    { let lo = min(p59, p51); p51 = max(p59, p51); p59 = lo; }
    { let lo = min(p60, p52); p52 = max(p60, p52); p60 = lo; }
    { let lo = min(p61, p53); p53 = max(p61, p53); p61 = lo; }
    { let lo = min(p62, p54); p54 = max(p62, p54); p62 = lo; }
    { let lo = min(p63, p55); p55 = max(p63, p55); p63 = lo; }
    { let lo = min(p0, p4); p4 = max(p0, p4); p0 = lo; }
    { let lo = min(p1, p5); p5 = max(p1, p5); p1 = lo; }
    { let lo = min(p2, p6); p6 = max(p2, p6); p2 = lo; }
    { let lo = min(p3, p7); p7 = max(p3, p7); p3 = lo; }
    { let lo = min(p8, p12); p12 = max(p8, p12); p8 = lo; }
    { let lo = min(p9, p13); p13 = max(p9, p13); p9 = lo; }
    { let lo = min(p10, p14); p14 = max(p10, p14); p10 = lo; }
    { let lo = min(p11, p15); p15 = max(p11, p15); p11 = lo; }
    { let lo = min(p16, p20); p20 = max(p16, p20); p16 = lo; }
    { let lo = min(p17, p21); p21 = max(p17, p21); p17 = lo; }
    { let lo = min(p18, p22); p22 = max(p18, p22); p18 = lo; }
    { let lo = min(p19, p23); p23 = max(p19, p23); p19 = lo; }
    { let lo = min(p24, p28); p28 = max(p24, p28); p24 = lo; }
    { let lo = min(p25, p29); p29 = max(p25, p29); p25 = lo; }
    { let lo = min(p26, p30); p30 = max(p26, p30); p26 = lo; }
    { let lo = min(p27, p31); p31 = max(p27, p31); p27 = lo; }
    { let lo = min(p36, p32); p32 = max(p36, p32); p36 = lo; }
    { let lo = min(p37, p33); p33 = max(p37, p33); p37 = lo; }
    { let lo = min(p38, p34); p34 = max(p38, p34); p38 = lo; }
    { let lo = min(p39, p35); p35 = max(p39, p35); p39 = lo; }
    { let lo = min(p44, p40); p40 = max(p44, p40); p44 = lo; }
    { let lo = min(p45, p41); p41 = max(p45, p41); p45 = lo; }
    { let lo = min(p46, p42); p42 = max(p46, p42); p46 = lo; }
    { let lo = min(p47, p43); p43 = max(p47, p43); p47 = lo; }
    { let lo = min(p52, p48); p48 = max(p52, p48); p52 = lo; }
    { let lo = min(p53, p49); p49 = max(p53, p49); p53 = lo; }
    { let lo = min(p54, p50); p50 = max(p54, p50); p54 = lo; }
    { let lo = min(p55, p51); p51 = max(p55, p51); p55 = lo; }
    { let lo = min(p60, p56); p56 = max(p60, p56); p60 = lo; }
    { let lo = min(p61, p57); p57 = max(p61, p57); p61 = lo; }
    { let lo = min(p62, p58); p58 = max(p62, p58); p62 = lo; }
    { let lo = min(p63, p59); p59 = max(p63, p59); p63 = lo; }
    { let lo = min(p0, p2); p2 = max(p0, p2); p0 = lo; }
    { let lo = min(p1, p3); p3 = max(p1, p3); p1 = lo; }
    { let lo = min(p4, p6); p6 = max(p4, p6); p4 = lo; }
    { let lo = min(p5, p7); p7 = max(p5, p7); p5 = lo; }
    { let lo = min(p8, p10); p10 = max(p8, p10); p8 = lo; }
    { let lo = min(p9, p11); p11 = max(p9, p11); p9 = lo; }
    { let lo = min(p12, p14); p14 = max(p12, p14); p12 = lo; }
    { let lo = min(p13, p15); p15 = max(p13, p15); p13 = lo; }
    { let lo = min(p16, p18); p18 = max(p16, p18); p16 = lo; }
    { let lo = min(p17, p19); p19 = max(p17, p19); p17 = lo; }
    { let lo = min(p20, p22); p22 = max(p20, p22); p20 = lo; }
    { let lo = min(p21, p23); p23 = max(p21, p23); p21 = lo; }
    { let lo = min(p24, p26); p26 = max(p24, p26); p24 = lo; }
    { let lo = min(p25, p27); p27 = max(p25, p27); p25 = lo; }
    { let lo = min(p28, p30); p30 = max(p28, p30); p28 = lo; }
    { let lo = min(p29, p31); p31 = max(p29, p31); p29 = lo; }
    { let lo = min(p34, p32); p32 = max(p34, p32); p34 = lo; }
    { let lo = min(p35, p33); p33 = max(p35, p33); p35 = lo; }
    { let lo = min(p38, p36); p36 = max(p38, p36); p38 = lo; }
    { let lo = min(p39, p37); p37 = max(p39, p37); p39 = lo; }
    { let lo = min(p42, p40); p40 = max(p42, p40); p42 = lo; }
    { let lo = min(p43, p41); p41 = max(p43, p41); p43 = lo; }
    { let lo = min(p46, p44); p44 = max(p46, p44); p46 = lo; }
    { let lo = min(p47, p45); p45 = max(p47, p45); p47 = lo; }
    { let lo = min(p50, p48); p48 = max(p50, p48); p50 = lo; }
    { let lo = min(p51, p49); p49 = max(p51, p49); p51 = lo; }
    { let lo = min(p54, p52); p52 = max(p54, p52); p54 = lo; }
    { let lo = min(p55, p53); p53 = max(p55, p53); p55 = lo; }
    { let lo = min(p58, p56); p56 = max(p58, p56); p58 = lo; }
    { let lo = min(p59, p57); p57 = max(p59, p57); p59 = lo; }
    { let lo = min(p62, p60); p60 = max(p62, p60); p62 = lo; }
    { let lo = min(p63, p61); p61 = max(p63, p61); p63 = lo; }
    { let lo = min(p0, p1); p1 = max(p0, p1); p0 = lo; }
    { let lo = min(p2, p3); p3 = max(p2, p3); p2 = lo; }
    { let lo = min(p4, p5); p5 = max(p4, p5); p4 = lo; }
    { let lo = min(p6, p7); p7 = max(p6, p7); p6 = lo; }
    { let lo = min(p8, p9); p9 = max(p8, p9); p8 = lo; }
    { let lo = min(p10, p11); p11 = max(p10, p11); p10 = lo; }
    { let lo = min(p12, p13); p13 = max(p12, p13); p12 = lo; }
    { let lo = min(p14, p15); p15 = max(p14, p15); p14 = lo; }
    { let lo = min(p16, p17); p17 = max(p16, p17); p16 = lo; }
    { let lo = min(p18, p19); p19 = max(p18, p19); p18 = lo; }
    { let lo = min(p20, p21); p21 = max(p20, p21); p20 = lo; }
    { let lo = min(p22, p23); p23 = max(p22, p23); p22 = lo; }
    { let lo = min(p24, p25); p25 = max(p24, p25); p24 = lo; }
    { let lo = min(p26, p27); p27 = max(p26, p27); p26 = lo; }
    { let lo = min(p28, p29); p29 = max(p28, p29); p28 = lo; }
    { let lo = min(p30, p31); p31 = max(p30, p31); p30 = lo; }
    { let lo = min(p33, p32); p32 = max(p33, p32); p33 = lo; }
    { let lo = min(p35, p34); p34 = max(p35, p34); p35 = lo; }
    { let lo = min(p37, p36); p36 = max(p37, p36); p37 = lo; }
    { let lo = min(p39, p38); p38 = max(p39, p38); p39 = lo; }
    { let lo = min(p41, p40); p40 = max(p41, p40); p41 = lo; }
    { let lo = min(p43, p42); p42 = max(p43, p42); p43 = lo; }
    { let lo = min(p45, p44); p44 = max(p45, p44); p45 = lo; }
    { let lo = min(p47, p46); p46 = max(p47, p46); p47 = lo; }
    { let lo = min(p49, p48); p48 = max(p49, p48); p49 = lo; }
    { let lo = min(p51, p50); p50 = max(p51, p50); p51 = lo; }
    { let lo = min(p53, p52); p52 = max(p53, p52); p53 = lo; }
    { let lo = min(p55, p54); p54 = max(p55, p54); p55 = lo; }
    { let lo = min(p57, p56); p56 = max(p57, p56); p57 = lo; }
    { let lo = min(p59, p58); p58 = max(p59, p58); p59 = lo; }
    { let lo = min(p61, p60); p60 = max(p61, p60); p61 = lo; }
    { let lo = min(p63, p62); p62 = max(p63, p62); p63 = lo; }
    { let lo = min(p0, p32); p32 = max(p0, p32); p0 = lo; }
    { let lo = min(p1, p33); p33 = max(p1, p33); p1 = lo; }
    { let lo = min(p2, p34); p34 = max(p2, p34); p2 = lo; }
    { let lo = min(p3, p35); p35 = max(p3, p35); p3 = lo; }
    { let lo = min(p4, p36); p36 = max(p4, p36); p4 = lo; }
    { let lo = min(p5, p37); p37 = max(p5, p37); p5 = lo; }
    { let lo = min(p6, p38); p38 = max(p6, p38); p6 = lo; }
    { let lo = min(p7, p39); p39 = max(p7, p39); p7 = lo; }
    { let lo = min(p8, p40); p40 = max(p8, p40); p8 = lo; }
    { let lo = min(p9, p41); p41 = max(p9, p41); p9 = lo; }
    { let lo = min(p10, p42); p42 = max(p10, p42); p10 = lo; }
    { let lo = min(p11, p43); p43 = max(p11, p43); p11 = lo; }
    { let lo = min(p12, p44); p44 = max(p12, p44); p12 = lo; }
    { let lo = min(p13, p45); p45 = max(p13, p45); p13 = lo; }
    { let lo = min(p14, p46); p46 = max(p14, p46); p14 = lo; }
    { let lo = min(p15, p47); p47 = max(p15, p47); p15 = lo; }
    { let lo = min(p16, p48); p48 = max(p16, p48); p16 = lo; }
    { let lo = min(p17, p49); p49 = max(p17, p49); p17 = lo; }
    { let lo = min(p18, p50); p50 = max(p18, p50); p18 = lo; }
    { let lo = min(p19, p51); p51 = max(p19, p51); p19 = lo; }
    { let lo = min(p20, p52); p52 = max(p20, p52); p20 = lo; }
    { let lo = min(p21, p53); p53 = max(p21, p53); p21 = lo; }
    { let lo = min(p22, p54); p54 = max(p22, p54); p22 = lo; }
    { let lo = min(p23, p55); p55 = max(p23, p55); p23 = lo; }
    { let lo = min(p24, p56); p56 = max(p24, p56); p24 = lo; }
    { let lo = min(p25, p57); p57 = max(p25, p57); p25 = lo; }
    { let lo = min(p26, p58); p58 = max(p26, p58); p26 = lo; }
    { let lo = min(p27, p59); p59 = max(p27, p59); p27 = lo; }
    { let lo = min(p28, p60); p60 = max(p28, p60); p28 = lo; }
    { let lo = min(p29, p61); p61 = max(p29, p61); p29 = lo; }
    { let lo = min(p30, p62); p62 = max(p30, p62); p30 = lo; }
    { let lo = min(p31, p63); p63 = max(p31, p63); p31 = lo; }
    { let lo = min(p0, p16); p16 = max(p0, p16); p0 = lo; }
    { let lo = min(p1, p17); p17 = max(p1, p17); p1 = lo; }
    { let lo = min(p2, p18); p18 = max(p2, p18); p2 = lo; }
    { let lo = min(p3, p19); p19 = max(p3, p19); p3 = lo; }
    { let lo = min(p4, p20); p20 = max(p4, p20); p4 = lo; }
    { let lo = min(p5, p21); p21 = max(p5, p21); p5 = lo; }
    { let lo = min(p6, p22); p22 = max(p6, p22); p6 = lo; }
    { let lo = min(p7, p23); p23 = max(p7, p23); p7 = lo; }
    { let lo = min(p8, p24); p24 = max(p8, p24); p8 = lo; }
    { let lo = min(p9, p25); p25 = max(p9, p25); p9 = lo; }
    { let lo = min(p10, p26); p26 = max(p10, p26); p10 = lo; }
    { let lo = min(p11, p27); p27 = max(p11, p27); p11 = lo; }
    { let lo = min(p12, p28); p28 = max(p12, p28); p12 = lo; }
    { let lo = min(p13, p29); p29 = max(p13, p29); p13 = lo; }
    { let lo = min(p14, p30); p30 = max(p14, p30); p14 = lo; }
    { let lo = min(p15, p31); p31 = max(p15, p31); p15 = lo; }
    { let lo = min(p32, p48); p48 = max(p32, p48); p32 = lo; }
    { let lo = min(p33, p49); p49 = max(p33, p49); p33 = lo; }
    { let lo = min(p34, p50); p50 = max(p34, p50); p34 = lo; }
    { let lo = min(p35, p51); p51 = max(p35, p51); p35 = lo; }
    { let lo = min(p36, p52); p52 = max(p36, p52); p36 = lo; }
    { let lo = min(p37, p53); p53 = max(p37, p53); p37 = lo; }
    { let lo = min(p38, p54); p54 = max(p38, p54); p38 = lo; }
    { let lo = min(p39, p55); p55 = max(p39, p55); p39 = lo; }
    { let lo = min(p40, p56); p56 = max(p40, p56); p40 = lo; }
    { let lo = min(p41, p57); p57 = max(p41, p57); p41 = lo; }
    { let lo = min(p42, p58); p58 = max(p42, p58); p42 = lo; }
    { let lo = min(p43, p59); p59 = max(p43, p59); p43 = lo; }
    { let lo = min(p44, p60); p60 = max(p44, p60); p44 = lo; }
    { let lo = min(p45, p61); p61 = max(p45, p61); p45 = lo; }
    { let lo = min(p46, p62); p62 = max(p46, p62); p46 = lo; }
    { let lo = min(p47, p63); p63 = max(p47, p63); p47 = lo; }
    { let lo = min(p0, p8); p8 = max(p0, p8); p0 = lo; }
    { let lo = min(p1, p9); p9 = max(p1, p9); p1 = lo; }
    { let lo = min(p2, p10); p10 = max(p2, p10); p2 = lo; }
    { let lo = min(p3, p11); p11 = max(p3, p11); p3 = lo; }
    { let lo = min(p4, p12); p12 = max(p4, p12); p4 = lo; }
    { let lo = min(p5, p13); p13 = max(p5, p13); p5 = lo; }
    { let lo = min(p6, p14); p14 = max(p6, p14); p6 = lo; }
    { let lo = min(p7, p15); p15 = max(p7, p15); p7 = lo; }
    { let lo = min(p16, p24); p24 = max(p16, p24); p16 = lo; }
    { let lo = min(p17, p25); p25 = max(p17, p25); p17 = lo; }
    { let lo = min(p18, p26); p26 = max(p18, p26); p18 = lo; }
    { let lo = min(p19, p27); p27 = max(p19, p27); p19 = lo; }
    { let lo = min(p20, p28); p28 = max(p20, p28); p20 = lo; }
    { let lo = min(p21, p29); p29 = max(p21, p29); p21 = lo; }
    { let lo = min(p22, p30); p30 = max(p22, p30); p22 = lo; }
    { let lo = min(p23, p31); p31 = max(p23, p31); p23 = lo; }
    { let lo = min(p32, p40); p40 = max(p32, p40); p32 = lo; }
    { let lo = min(p33, p41); p41 = max(p33, p41); p33 = lo; }
    { let lo = min(p34, p42); p42 = max(p34, p42); p34 = lo; }
    { let lo = min(p35, p43); p43 = max(p35, p43); p35 = lo; }
    { let lo = min(p36, p44); p44 = max(p36, p44); p36 = lo; }
    { let lo = min(p37, p45); p45 = max(p37, p45); p37 = lo; }
    { let lo = min(p38, p46); p46 = max(p38, p46); p38 = lo; }
    { let lo = min(p39, p47); p47 = max(p39, p47); p39 = lo; }
    { let lo = min(p48, p56); p56 = max(p48, p56); p48 = lo; }
    { let lo = min(p49, p57); p57 = max(p49, p57); p49 = lo; }
    { let lo = min(p50, p58); p58 = max(p50, p58); p50 = lo; }
    { let lo = min(p51, p59); p59 = max(p51, p59); p51 = lo; }
    { let lo = min(p52, p60); p60 = max(p52, p60); p52 = lo; }
    { let lo = min(p53, p61); p61 = max(p53, p61); p53 = lo; }
    { let lo = min(p54, p62); p62 = max(p54, p62); p54 = lo; }
    { let lo = min(p55, p63); p63 = max(p55, p63); p55 = lo; }
    { let lo = min(p0, p4); p4 = max(p0, p4); p0 = lo; }
    { let lo = min(p1, p5); p5 = max(p1, p5); p1 = lo; }
    { let lo = min(p2, p6); p6 = max(p2, p6); p2 = lo; }
    { let lo = min(p3, p7); p7 = max(p3, p7); p3 = lo; }
    { let lo = min(p8, p12); p12 = max(p8, p12); p8 = lo; }
    { let lo = min(p9, p13); p13 = max(p9, p13); p9 = lo; }
    { let lo = min(p10, p14); p14 = max(p10, p14); p10 = lo; }
    { let lo = min(p11, p15); p15 = max(p11, p15); p11 = lo; }
    { let lo = min(p16, p20); p20 = max(p16, p20); p16 = lo; }
    { let lo = min(p17, p21); p21 = max(p17, p21); p17 = lo; }
    { let lo = min(p18, p22); p22 = max(p18, p22); p18 = lo; }
    { let lo = min(p19, p23); p23 = max(p19, p23); p19 = lo; }
    { let lo = min(p24, p28); p28 = max(p24, p28); p24 = lo; }
    { let lo = min(p25, p29); p29 = max(p25, p29); p25 = lo; }
    { let lo = min(p26, p30); p30 = max(p26, p30); p26 = lo; }
    { let lo = min(p27, p31); p31 = max(p27, p31); p27 = lo; }
    { let lo = min(p32, p36); p36 = max(p32, p36); p32 = lo; }
    { let lo = min(p33, p37); p37 = max(p33, p37); p33 = lo; }
    { let lo = min(p34, p38); p38 = max(p34, p38); p34 = lo; }
    { let lo = min(p35, p39); p39 = max(p35, p39); p35 = lo; }
    { let lo = min(p40, p44); p44 = max(p40, p44); p40 = lo; }
    { let lo = min(p41, p45); p45 = max(p41, p45); p41 = lo; }
    { let lo = min(p42, p46); p46 = max(p42, p46); p42 = lo; }
    { let lo = min(p43, p47); p47 = max(p43, p47); p43 = lo; }
    { let lo = min(p48, p52); p52 = max(p48, p52); p48 = lo; }
    { let lo = min(p49, p53); p53 = max(p49, p53); p49 = lo; }
    { let lo = min(p50, p54); p54 = max(p50, p54); p50 = lo; }
    { let lo = min(p51, p55); p55 = max(p51, p55); p51 = lo; }
    { let lo = min(p56, p60); p60 = max(p56, p60); p56 = lo; }
    { let lo = min(p57, p61); p61 = max(p57, p61); p57 = lo; }
    { let lo = min(p58, p62); p62 = max(p58, p62); p58 = lo; }
    { let lo = min(p59, p63); p63 = max(p59, p63); p59 = lo; }
    { let lo = min(p0, p2); p2 = max(p0, p2); p0 = lo; }
    { let lo = min(p1, p3); p3 = max(p1, p3); p1 = lo; }
    { let lo = min(p4, p6); p6 = max(p4, p6); p4 = lo; }
    { let lo = min(p5, p7); p7 = max(p5, p7); p5 = lo; }
    { let lo = min(p8, p10); p10 = max(p8, p10); p8 = lo; }
    { let lo = min(p9, p11); p11 = max(p9, p11); p9 = lo; }
    { let lo = min(p12, p14); p14 = max(p12, p14); p12 = lo; }
    { let lo = min(p13, p15); p15 = max(p13, p15); p13 = lo; }
    { let lo = min(p16, p18); p18 = max(p16, p18); p16 = lo; }
    { let lo = min(p17, p19); p19 = max(p17, p19); p17 = lo; }
    { let lo = min(p20, p22); p22 = max(p20, p22); p20 = lo; }
    { let lo = min(p21, p23); p23 = max(p21, p23); p21 = lo; }
    { let lo = min(p24, p26); p26 = max(p24, p26); p24 = lo; }
    { let lo = min(p25, p27); p27 = max(p25, p27); p25 = lo; }
    { let lo = min(p28, p30); p30 = max(p28, p30); p28 = lo; }
    { let lo = min(p29, p31); p31 = max(p29, p31); p29 = lo; }
    { let lo = min(p32, p34); p34 = max(p32, p34); p32 = lo; }
    { let lo = min(p33, p35); p35 = max(p33, p35); p33 = lo; }
    { let lo = min(p36, p38); p38 = max(p36, p38); p36 = lo; }
    { let lo = min(p37, p39); p39 = max(p37, p39); p37 = lo; }
    { let lo = min(p40, p42); p42 = max(p40, p42); p40 = lo; }
    { let lo = min(p41, p43); p43 = max(p41, p43); p41 = lo; }
    { let lo = min(p44, p46); p46 = max(p44, p46); p44 = lo; }
    { let lo = min(p45, p47); p47 = max(p45, p47); p45 = lo; }
    { let lo = min(p48, p50); p50 = max(p48, p50); p48 = lo; }
    { let lo = min(p49, p51); p51 = max(p49, p51); p49 = lo; }
    { let lo = min(p52, p54); p54 = max(p52, p54); p52 = lo; }
    { let lo = min(p53, p55); p55 = max(p53, p55); p53 = lo; }
    { let lo = min(p56, p58); p58 = max(p56, p58); p56 = lo; }
    { let lo = min(p57, p59); p59 = max(p57, p59); p57 = lo; }
    { let lo = min(p60, p62); p62 = max(p60, p62); p60 = lo; }
    { let lo = min(p61, p63); p63 = max(p61, p63); p61 = lo; }
    { let lo = min(p0, p1); p1 = max(p0, p1); p0 = lo; }
    { let lo = min(p2, p3); p3 = max(p2, p3); p2 = lo; }
    { let lo = min(p4, p5); p5 = max(p4, p5); p4 = lo; }
    { let lo = min(p6, p7); p7 = max(p6, p7); p6 = lo; }
    { let lo = min(p8, p9); p9 = max(p8, p9); p8 = lo; }
    { let lo = min(p10, p11); p11 = max(p10, p11); p10 = lo; }
    { let lo = min(p12, p13); p13 = max(p12, p13); p12 = lo; }
    { let lo = min(p14, p15); p15 = max(p14, p15); p14 = lo; }
    { let lo = min(p16, p17); p17 = max(p16, p17); p16 = lo; }
    { let lo = min(p18, p19); p19 = max(p18, p19); p18 = lo; }
    { let lo = min(p20, p21); p21 = max(p20, p21); p20 = lo; }
    { let lo = min(p22, p23); p23 = max(p22, p23); p22 = lo; }
    { let lo = min(p24, p25); p25 = max(p24, p25); p24 = lo; }
    { let lo = min(p26, p27); p27 = max(p26, p27); p26 = lo; }
    { let lo = min(p28, p29); p29 = max(p28, p29); p28 = lo; }
    { let lo = min(p30, p31); p31 = max(p30, p31); p30 = lo; }
    { let lo = min(p32, p33); p33 = max(p32, p33); p32 = lo; }
    { let lo = min(p34, p35); p35 = max(p34, p35); p34 = lo; }
    { let lo = min(p36, p37); p37 = max(p36, p37); p36 = lo; }
    { let lo = min(p38, p39); p39 = max(p38, p39); p38 = lo; }
    { let lo = min(p40, p41); p41 = max(p40, p41); p40 = lo; }
    { let lo = min(p42, p43); p43 = max(p42, p43); p42 = lo; }
    { let lo = min(p44, p45); p45 = max(p44, p45); p44 = lo; }
    { let lo = min(p46, p47); p47 = max(p46, p47); p46 = lo; }
    { let lo = min(p48, p49); p49 = max(p48, p49); p48 = lo; }
    { let lo = min(p50, p51); p51 = max(p50, p51); p50 = lo; }
    { let lo = min(p52, p53); p53 = max(p52, p53); p52 = lo; }
    { let lo = min(p54, p55); p55 = max(p54, p55); p54 = lo; }
    { let lo = min(p56, p57); p57 = max(p56, p57); p56 = lo; }
    { let lo = min(p58, p59); p59 = max(p58, p59); p58 = lo; }
    { let lo = min(p60, p61); p61 = max(p60, p61); p60 = lo; }
    { let lo = min(p62, p63); p63 = max(p62, p63); p62 = lo; }
    return p24;
}
fn apply_operation(id: vec3<u32>) {
    var value = vec4<f32>(0.0);
    if (params.offsets.x == 2) { value = median2(vec2<i32>(id.xy)); }
    else { value = median3(vec2<i32>(id.xy)); }
    destination[id.y * params.dimensions.x + id.x] = select(value, vec4<f32>(0.0), value.a == 0.0);
}
