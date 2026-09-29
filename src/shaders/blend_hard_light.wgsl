fn blend_hard_light(backdrop: vec3<f32>, foreground: vec3<f32>) -> vec3<f32> {
    return select(vec3<f32>(1.0) - 2.0 * (vec3<f32>(1.0) - backdrop) *
                                      (vec3<f32>(1.0) - foreground),
                  2.0 * backdrop * foreground,
                  foreground <= vec3<f32>(0.5));
}
