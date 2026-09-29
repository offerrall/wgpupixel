#include "painting_reference.h"
#include <limits>
using namespace wgpupixel;
namespace {
std::vector<std::uint8_t> read_mask(Context& ctx, const Mask& mask) {
    auto buffer = ctx.create_readback_buffer(mask);
    ctx.run_and_wait([&](Commands& cmd) { cmd.download(mask, buffer); });
    std::vector<std::uint8_t> bytes(mask.size().width * mask.size().height);
    ctx.read(buffer, bytes);
    return bytes;
}
} // namespace
int main() {
    test::run("explicit stamps accumulate by flow and erasing scales all channels", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({19, 13});
        const std::array samples{StrokeSample{{9.5f, 6.5f}}, StrokeSample{{9.5f, 6.5f}}};
        const Brush brush{.diameter = 100, .spacing = 0};
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.fill(image, {.color = {0, 0, 0, 0}});
            cmd.brush_stroke(
                image, {.samples = samples, .brush = brush, .color = {-2, 3, 0, 1}, .flow = 0.5f});
            cmd.eraser_stroke(
                image, {.samples = std::span(samples).first(1), .brush = brush, .opacity = 0.25f});
        });
        for (const auto& p : reference::download(ctx, image).pixels) {
            // Two half-flow stamps cover 3/4, then erase 1/4 of every channel.
            test::near(p[0], -1.125f);
            test::near(p[1], 1.6875f);
            test::near(p[3], 0.5625f);
        }
    });
    test::run("mask brush and eraser preserve packed neighbors and honor selection and region", [] {
        auto ctx = Context::create();
        auto target = ctx.create_mask({19, 13});
        auto selection = ctx.create_mask(target.size());
        auto tip = ctx.create_mask({3, 3});
        const std::array samples{StrokeSample{{9.5f, 6.5f}}};
        for (bool sampled : {false, true}) {
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.fill(target, {.coverage = 64.0f / 255});
                cmd.fill(selection, {.coverage = 128.0f / 255});
                cmd.fill(tip, {.coverage = 1});
                cmd.brush_stroke(target,
                                 {.samples = samples,
                                  .brush = {.diameter = 100, .tip = sampled ? &tip : nullptr},
                                  .coverage = 1,
                                  .opacity = 0.5f,
                                  .mask = &selection,
                                  .region = Rect{3, 2, 11, 8}});
            });
            auto bytes = read_mask(ctx, target);
            for (int y = 0; y < 13; ++y) {
                for (int x = 0; x < 19; ++x) {
                    test::near(bytes[y * 19 + x], x >= 3 && x < 14 && y >= 2 && y < 10 ? 112 : 64,
                               0);
                }
            }
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.eraser_stroke(target, {.samples = samples,
                                           .brush = {.diameter = 100},
                                           .opacity = 0.5f,
                                           .region = Rect{3, 2, 11, 8}});
            });
            bytes = read_mask(ctx, target);
            for (int y = 0; y < 13; ++y) {
                for (int x = 0; x < 19; ++x) {
                    test::near(bytes[y * 19 + x], x >= 3 && x < 14 && y >= 2 && y < 10 ? 56 : 64,
                               0);
                }
            }
        }
        auto cmd = ctx.create_commands();
        test::error(ErrorCode::invalid_argument, "brush_stroke", "mask",
                    [&] { cmd.brush_stroke(target, {.samples = samples, .mask = &target}); });
        test::error(ErrorCode::invalid_argument, "brush_stroke", "coverage",
                    [&] { cmd.brush_stroke(target, {.samples = samples, .coverage = 2}); });
    });
    test::run("alpha lock on painting tools uses destination alpha", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({19, 13});
        auto source = ctx.create_image(image.size());
        auto focus_workspace =
            ctx.create_workspace(focus_stroke_requirements(image.size()).workspace);
        auto selection = ctx.create_mask(image.size());
        const std::array samples{StrokeSample{{9.5f, 6.5f}}};
        const std::array ramp{GradientStop{0, {-1, 2, 0, 1}}};
        auto original = reference::pattern(19, 13, 4);
        for (std::size_t i = 0; i < original.pixels.size(); ++i) {
            double a = i % 3 == 0 ? 0 : i % 3 == 1 ? 1e-6 : 0.5;
            original.pixels[i] = {-2 * a, 3 * a, 0, a};
        }
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.fill(source, {.color = {-1, 2, 0, 1}});
            cmd.fill(selection, {.coverage = 128.0f / 255});
        });
        for (int tool = 0; tool < 8; ++tool) {
            reference::upload(ctx, image, original);
            ctx.run_and_wait([&](Commands& cmd) {
                if (tool == 0) {
                    cmd.brush_stroke(image, {.samples = samples,
                                             .brush = {.diameter = 100},
                                             .color = {-1, 2, 0, 1},
                                             .opacity = 0.5f,
                                             .preserve_alpha = true,
                                             .mask = &selection});
                }
                if (tool == 1) {
                    cmd.clone_stroke(source, image,
                                     {.samples = samples,
                                      .brush = {.diameter = 100},
                                      .opacity = 0.5f,
                                      .preserve_alpha = true,
                                      .mask = &selection});
                }
                if (tool == 2) {
                    cmd.pattern_stroke(source, image,
                                       {.samples = samples,
                                        .brush = {.diameter = 100},
                                        .opacity = 0.5f,
                                        .preserve_alpha = true,
                                        .mask = &selection});
                }
                if (tool == 3) {
                    cmd.pattern_fill(source, image,
                                     {.opacity = 0.5f, .preserve_alpha = true, .mask = &selection});
                }
                if (tool == 4) {
                    cmd.gradient_fill(image, {.end = {1, 0},
                                              .stops = ramp,
                                              .dither = false,
                                              .opacity = 0.5f,
                                              .preserve_alpha = true,
                                              .mask = &selection});
                }
                if (tool == 5) {
                    cmd.paint_bucket(image, {.seed = {-99, -99},
                                             .tolerance = std::numeric_limits<float>::quiet_NaN(),
                                             .color = {-1, 2, 0, 1},
                                             .opacity = 0.5f,
                                             .match_seed = false,
                                             .preserve_alpha = true,
                                             .mask = &selection});
                }
                if (tool >= 6) {
                    cmd.focus_stroke(image, {.samples = samples,
                                             .brush = {.diameter = 100},
                                             .sharpen = tool == 7,
                                             .strength = 1,
                                             .preserve_alpha = true,
                                             .mask = &selection,
                                             .region = Rect{3, 2, 11, 8},
                                             .workspace = focus_workspace});
                }
            });
            auto result = reference::download(ctx, image);
            for (std::size_t i = 0; i < result.pixels.size(); ++i) {
                const double a = original.pixels[i][3], t = tool < 6 ? 0.5 * 128 / 255 : 0;
                test::near(result.pixels[i][0], (-2 + t) * a, 1e-6f);
                test::near(result.pixels[i][1], (3 - t) * a, 1e-6f);
                test::near(result.pixels[i][3], a, 1e-9f);
            }
        }
    });
    test::run("continued strokes equal one call across submissions including direction jitter and "
              "pressure",
              [] {
                  auto ctx = Context::create();
                  auto whole = ctx.create_image({41, 29});
                  auto split = ctx.create_image(whole.size());
                  auto mask = ctx.create_mask(whole.size());
                  auto mw = ctx.create_mask(whole.size());
                  auto ms = ctx.create_mask(whole.size());
                  const std::array samples{StrokeSample{{4, 6}, 0.3f}, StrokeSample{{18, 10}, 0.9f},
                                           StrokeSample{{30, 23}, 0.6f}, StrokeSample{{37, 9}, 1}};
                  const Brush brush{.diameter = 12,
                                    .hardness = 0.3f,
                                    .spacing = 0.17f,
                                    .minimum_size = 0.2f,
                                    .opacity_jitter = 0.4f,
                                    .angle_jitter = 0.7f,
                                    .angle_control = BrushAngleControl::direction,
                                    .scatter = 0.1f,
                                    .seed = 37};
                  auto scratch = ctx.create_workspace(
                      smudge_stroke_requirements({1, 1}, {.brush = brush}).workspace);
                  auto original = reference::pattern(41, 29, 7);
                  for (int tool = 0; tool < 5; ++tool) {
                      reference::upload(ctx, whole, original);
                      reference::upload(ctx, split, original);
                      ctx.run_and_wait([&](Commands& cmd) {
                          cmd.fill(mask, {.coverage = 128.0f / 255});
                          cmd.fill(mw, {.coverage = 0.25f});
                          cmd.fill(ms, {.coverage = 0.25f});
                      });
                      BrushStrokeState state;
                      if (tool != 4) {
                          state = (tool == 2 || tool == 3) ? ctx.create_brush_stroke_state(ms)
                                                           : ctx.create_brush_stroke_state(split);
                      }
                      auto smudge =
                          tool == 4 ? ctx.create_smudge_stroke_state(split) : SmudgeStrokeState{};
                      auto apply = [&](Commands& cmd, bool part,
                                       std::span<const StrokeSample> events) {
                          const auto& dst = part ? split : whole;
                          const auto& mdst = part ? ms : mw;
                          BrushStrokeOptions paint{.samples = events,
                                                   .brush = brush,
                                                   .color = {-0.2f, 0.6f, 0.1f, 0.7f},
                                                   .opacity = 0.6f,
                                                   .flow = 0.4f,
                                                   .mask = &mask,
                                                   .region = Rect{2, 3, 36, 23}};
                          EraserStrokeOptions erase{.samples = events,
                                                    .brush = brush,
                                                    .opacity = 0.6f,
                                                    .flow = 0.4f,
                                                    .mask = &mask,
                                                    .region = Rect{2, 3, 36, 23}};
                          MaskBrushStrokeOptions mp{.samples = events,
                                                    .brush = brush,
                                                    .coverage = 0.9f,
                                                    .opacity = 0.6f,
                                                    .flow = 0.4f,
                                                    .mask = &mask,
                                                    .region = Rect{2, 3, 36, 23}};
                          SmudgeStrokeOptions sm{.samples = events,
                                                 .brush = brush,
                                                 .strength = 0.7f,
                                                 .mask = &mask,
                                                 .region = Rect{2, 3, 36, 23}};
                          if (tool == 0) {
                              if (part) {
                                  cmd.brush_stroke(dst, state, paint);
                              } else {
                                  cmd.brush_stroke(dst, paint);
                              }
                          }
                          if (tool == 1) {
                              if (part) {
                                  cmd.eraser_stroke(dst, state, erase);
                              } else {
                                  cmd.eraser_stroke(dst, erase);
                              }
                          }
                          if (tool == 2) {
                              if (part) {
                                  cmd.brush_stroke(mdst, state, mp);
                              } else {
                                  cmd.brush_stroke(mdst, mp);
                              }
                          }
                          if (tool == 3) {
                              if (part) {
                                  cmd.eraser_stroke(mdst, state, erase);
                              } else {
                                  cmd.eraser_stroke(mdst, erase);
                              }
                          }
                          if (tool == 4) {
                              if (part) {
                                  cmd.smudge_stroke(dst, smudge, test::with_workspace(sm, scratch));
                              } else {
                                  cmd.smudge_stroke(dst, test::with_workspace(sm, scratch));
                              }
                          }
                      };
                      ctx.run_and_wait([&](Commands& cmd) { apply(cmd, false, samples); });
                      for (int i = 0; i < 4; ++i) {
                          ctx.run_and_wait([&](Commands& cmd) {
                              apply(cmd, true, std::span(samples).subspan(i, 1));
                          });
                      }
                      if (tool == 2 || tool == 3) {
                          test::check(read_mask(ctx, mw) == read_mask(ctx, ms),
                                      "mask continuation differs");
                      } else {
                          reference::expect(reference::download(ctx, split),
                                            reference::download(ctx, whole), 0);
                      }
                  }
              });
    test::run("dense brush dispatch pieces retain the whole stroke opacity cap", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({67, 49});
        std::vector<StrokeSample> samples(8000, StrokeSample{{33.5f, 24.5f}});
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.fill(image, {.color = {0, 1, 0, 1}});
            cmd.brush_stroke(image, {.samples = samples,
                                     .brush = {.diameter = 5000, .spacing = 0},
                                     .color = {-1, 2, 0, 1},
                                     .opacity = 0.4f});
        });
        for (const auto& p : reference::download(ctx, image).pixels) {
            test::near(p[0], -0.4f);
            test::near(p[1], 1.4f);
            test::near(p[3], 1);
        }
    });
    test::run(
        "smudge tiles transport pigment across a 1024 pixel patch boundary with alpha lock", [] {
            auto ctx = Context::create();
            auto image = ctx.create_image({1107, 5});
            const Brush brush{.diameter = 1100, .spacing = 0};
            auto scratch = ctx.create_workspace(
                smudge_stroke_requirements({1, 1}, {.brush = brush}).workspace);
            const std::array samples{StrokeSample{{552.5f, 2.5f}}, StrokeSample{{553.5f, 2.5f}}};
            for (bool lock : {false, true}) {
                ctx.run_and_wait([&](Commands& cmd) {
                    cmd.fill(image, {.color = {0, 0, 1, 1}});
                    cmd.smudge_stroke(image, {.samples = samples,
                                              .brush = brush,
                                              .strength = 1,
                                              .finger_painting = true,
                                              .color = {-1, 2, 0, 0.5f},
                                              .preserve_alpha = lock,
                                              .region = Rect{0, 1, 1107, 3},
                                              .workspace = scratch});
                });
                const auto result = reference::download(ctx, image);
                for (int x : {100, 550, 1000, 1050}) {
                    test::near(result.at(x, 2)[0], lock ? -2 : -1);
                    test::near(result.at(x, 2)[1], lock ? 4 : 2);
                    test::near(result.at(x, 2)[3], lock ? 1 : 0.5f);
                    test::near(result.at(x, 0)[2], 1);
                }
                test::near(result.at(0, 2)[2], 1);
            }
        });
    return test::finish();
}
