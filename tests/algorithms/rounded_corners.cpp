#include "../test.h"
#include <limits>
using namespace wgpupixel;
int main() {
    test::run("rounded corners preserve straight colors and scale all RGBA", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({6, 4});
        auto cmd = ctx.create_commands(2);
        for (float radius : {0.f, 1.f, 2.f}) {
            cmd.fill(image, {.color = {.2f, .3f, .4f, .5f}});
            cmd.rounded_corners(image, {.radius = radius});
            ctx.submit_and_wait(cmd);
            std::vector<float> expected(6 * 4 * 4);
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 6; ++x) {
                    const float dx = std::abs(x + .5f - 3) - (3 - radius),
                                dy = std::abs(y + .5f - 2) - (2 - radius);
                    float c = 1;
                    if (dx > 0 && dy > 0) {
                        float t = std::clamp(radius - std::hypot(dx, dy), 0.f, 1.f);
                        c = t * t * (3 - 2 * t);
                    }
                    const auto i = (y * 6 + x) * 4;
                    expected[i] = .2f * c;
                    expected[i + 1] = .3f * c;
                    expected[i + 2] = .4f * c;
                    expected[i + 3] = .5f * c;
                }
            }
            const auto a = test::read(ctx, image), b = test::encode(expected);
            for (std::size_t i = 0; i < a.size(); ++i) {
                test::check(std::abs(int(a[i]) - int(b[i])) <= 1, "rounded coverage mismatch");
            }
        }
    });
    test::run("rounded corners invalid radius does not consume capacity", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({4, 2});
        auto cmd = ctx.create_commands(1);
        for (float r : {-1.f, 1.1f, std::numeric_limits<float>::quiet_NaN()}) {
            test::error(ErrorCode::invalid_argument, "rounded_corners", "radius",
                        [&] { cmd.rounded_corners(image, {.radius = r}); });
        }
        cmd.fill(image, {.color = {0, 0, 0, 0}});
        ctx.submit_and_wait(cmd);
    });
    return test::finish();
}
