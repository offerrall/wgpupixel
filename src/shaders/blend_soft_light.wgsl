fn blend_soft_light(backdrop: vec3<f32>, foreground: vec3<f32>) -> vec3<f32> {
    let d = select(sqrt(max(backdrop, vec3<f32>(0.0))),
                   ((16.0 * backdrop - 12.0) * backdrop + 4.0) * backdrop,
                   backdrop <= vec3<f32>(0.25));
    return select(backdrop + (2.0 * foreground - 1.0) * (d - backdrop),
                  backdrop - (1.0 - 2.0 * foreground) * backdrop * (1.0 - backdrop),
                  foreground <= vec3<f32>(0.5));
}
