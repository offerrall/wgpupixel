# Stroke continuation at 24 MP

`stroke_continuation.cpp` reproduces the editor's 200-sample diagonal stroke on a
6000×4000 image: diameter 40, two new samples per frame, opacity 0.5, final bounds
2028×1035. It includes CPU recording, submission and GPU completion. One gesture
warms the resources; three more provide 300 measured frames. No readback occurs
in the measured loop.

Build after the Release library (adjust the SDK path on other machines):

```sh
g++ -O3 -std=c++23 benchmarks/native/stroke_continuation.cpp -Iinclude \
    build/libwgpupixel.a \
    -L/home/offerrall/photoff_ecosistema/wgpupixel/build/native/wgpu-sdk/lib \
    -Wl,-rpath,/home/offerrall/photoff_ecosistema/wgpupixel/build/native/wgpu-sdk/lib \
    -lwgpu_native -o build/stroke_continuation_probe
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json build/stroke_continuation_probe
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json build/stroke_continuation_probe full-copy
```

The `full-copy` control implements the previous algorithm: capture the entire
canvas on the first frame, restore it on subsequent frames, replay all samples.
Both modes reserve one 384,000,000-byte snapshot before painting. The new algorithm
copies at most 33,583,680 logical bytes on the final frame and less on earlier
frames, versus 384,000,000 every frame in the control. GPU reads and writes each
contribute to physical memory traffic; these figures count logical copied pixels.

Measured on 2026-09-30, Release, Radeon 680M using RADV:

| Mode | Mean ms/call over 300 frames | Three gesture means, ms/call |
| --- | ---: | --- |
| Region continuation | 1.86 | 2.21, 1.58, 1.79 |
| Full-copy control | 27.66 | 27.07, 27.36, 28.53 |

The GPU is shared with other work, so these timings are observations, not test
thresholds. The first gesture (including initial buffer use) is excluded in both
modes. Replay remains cumulative in stroke length; this change removes the
per-frame canvas copy, not the documented dab/data work limits.

The regression test `wgpupixel.stroke_region` uses native command interception to
count buffer-copy bytes and compute workgroups, independently of timing. Capture
and restore use at most five disjoint rectangles, totalling the accumulated bounds;
the test allows only their 8×8 workgroup padding. It also checks identical copy and
replay work for the same stroke on 512×512 and 6000×4000 canvases (1024×1024 instead
on devices whose binding limit cannot fit 24 MP). It exercises brush, eraser,
A8-mask painting and smudge. These deterministic checks run on Linux static builds;
the exact-result tests run on every build.
