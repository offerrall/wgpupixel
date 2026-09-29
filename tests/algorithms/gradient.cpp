#include "painting_reference.h"
using namespace wgpupixel;
int main() {
    test::run("gradient replacement interpolates linear premultiplied stops including transparency",
              [] {
                  auto ctx = Context::create();
                  for (const auto size : {ImageSize{19, 13}, ImageSize{1, 1}, ImageSize{1, 17}}) {
                      auto image = ctx.create_image(size);
                      const std::array stops{GradientStop{0, {-2, 3, 0, 0.5f}},
                                             GradientStop{1, {0, 0, 0, 0}}};
                      ctx.run_and_wait([&](Commands& cmd) {
                          cmd.fill(image, {.color = {1, 0, 0, 1}});
                          cmd.gradient_fill(image, {.start = {0.5f, 0},
                                                    .end = {18.5f, 0},
                                                    .stops = stops,
                                                    .interpolation = GradientInterpolation::linear,
                                                    .dither = false,
                                                    .replace = true});
                      });
                      const auto result = reference::download(ctx, image);
                      for (int y = 0; y < size.height; ++y) {
                          for (int x = 0; x < size.width; ++x) {
                              const double remaining = 1 - x / 18.0;
                              const auto& p = result.at(x, y);
                              test::near(p[0], -2 * remaining);
                              test::near(p[1], 3 * remaining);
                              test::near(p[3], 0.5 * remaining);
                          }
                      }
                  }
              });
    test::run(
        "gradient replacement respects opacity selection region and validates stops atomically",
        [] {
            auto ctx = Context::create();
            auto image = ctx.create_image({19, 13});
            auto mask = ctx.create_mask(image.size());
            const std::array clear{GradientStop{0, {0, 0, 0, 0}}};
            auto cmd = ctx.create_commands();
            cmd.fill(image, {.color = {-1, 2, 0, 0.5f}});
            cmd.fill(mask, {.coverage = 128.0f / 255});
            const std::array bad{GradientStop{0, {0, 0, 0, 2}}};
            test::error(ErrorCode::invalid_argument, "gradient_fill", "stops",
                        [&] { cmd.gradient_fill(image, {.end = {1, 0}, .stops = bad}); });
            cmd.gradient_fill(image, {.end = {1, 0},
                                      .stops = clear,
                                      .dither = false,
                                      .opacity = 0.5f,
                                      .replace = true,
                                      .mask = &mask,
                                      .region = Rect{3, 2, 11, 8}});
            ctx.submit_and_wait(cmd);
            auto result = reference::download(ctx, image);
            for (int y = 0; y < 13; ++y) {
                for (int x = 0; x < 19; ++x) {
                    const float k = x >= 3 && x < 14 && y >= 2 && y < 10 ? 1 - 0.5f * 128 / 255 : 1;
                    test::near(result.at(x, y)[0], -k);
                    test::near(result.at(x, y)[1], 2 * k);
                    test::near(result.at(x, y)[3], 0.5f * k);
                }
            }
        });
    test::run("angle gradient sweeps clockwise through cardinal directions", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({5, 5});
        const std::array stops{GradientStop{0, {0, 0, 0, 1}}, GradientStop{1, {1, 1, 1, 1}}};
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.gradient_fill(image, {.start = {2.5f, 2.5f},
                                      .end = {3.5f, 2.5f},
                                      .stops = stops,
                                      .shape = GradientShape::angle,
                                      .interpolation = GradientInterpolation::linear,
                                      .dither = false,
                                      .replace = true});
        });
        const auto result = reference::download(ctx, image);
        test::near(result.at(3, 2)[0], 0);
        test::near(result.at(2, 3)[0], 0.25f);
        test::near(result.at(1, 2)[0], 0.5f);
        test::near(result.at(2, 1)[0], 0.75f);
    });
    return test::finish();
}
