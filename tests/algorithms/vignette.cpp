#include "../test.h"
#include <array>
#include <limits>
using namespace wgpupixel;
int main() {
    test::run("vignette CPU coverage and premultiplied tint preserve alpha", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({5, 3});
        auto cmd = ctx.create_commands(2);
        for (float softness : {0.f, .6f}) {
            cmd.fill(image, {.color = {.2f, .3f, .4f, .5f}});
            cmd.vignette(image, {.radius = .8f, .softness = softness, .color = {.4f, .1f, 0, .5f}});
            ctx.submit_and_wait(cmd);
            std::array<float, 60> expected{};
            for (int y = 0; y < 3; ++y) {
                for (int x = 0; x < 5; ++x) {
                    const auto d = std::hypot(x * .5f - 1, y - 1.f);
                    float t = softness == 0 ? (d >= .8f ? 1 : 0)
                                            : std::clamp((d - .8f) / softness + .5f, 0.f, 1.f);
                    const float c = softness == 0 ? t : t * t * (3 - 2 * t);
                    const auto i = (y * 5 + x) * 4;
                    expected[i] = .2f * (1 - .5f * c) + .4f * .5f * c;
                    expected[i + 1] = .3f * (1 - .5f * c) + .1f * .5f * c;
                    expected[i + 2] = .4f * (1 - .5f * c);
                    expected[i + 3] = .5f;
                }
            }
            const auto a = test::read(ctx, image), b = test::encode(expected);
            for (std::size_t i = 0; i < a.size(); ++i) {
                test::check(std::abs(int(a[i]) - int(b[i])) <= 1, "vignette mismatch");
            }
        }
        auto one = ctx.create_image({1, 1});
        cmd.fill(one, {.color = {.2f, .3f, .4f, .5f}});
        cmd.vignette(one, {.radius = .8f, .softness = 0, .color = {0, 0, 0, 1}});
        ctx.submit_and_wait(cmd);
        test::check(test::read(ctx, one)[0] == test::channel(.4f), "onepixel division byzero");
    });
    test::run("vignette rejects invalid values atomically", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        auto cmd = ctx.create_commands(1);
        test::error(ErrorCode::invalid_argument, "vignette", "radius", [&] {
            cmd.vignette(image, {.radius = -1, .softness = 1, .color = {0, 0, 0, 1}});
        });
        test::error(ErrorCode::invalid_argument, "vignette", "softness", [&] {
            cmd.vignette(image, {.radius = 1,
                                 .softness = std::numeric_limits<float>::infinity(),
                                 .color = {0, 0, 0, 1}});
        });
        cmd.fill(image, {.color = {0, 0, 0, 0}});
        ctx.submit_and_wait(cmd);
        cmd.vignette(image, {.radius = 0, .softness = 0, .color = {1, 0, 0, 1}});
        ctx.submit_and_wait(cmd);
        test::check(test::read(ctx, image) == std::vector<std::uint8_t>(4),
                    "transparent pixel recolored");
    });
    return test::finish();
}
