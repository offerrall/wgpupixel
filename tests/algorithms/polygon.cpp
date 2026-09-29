#include "../test.h"
#include <array>
#include <limits>
#include <numbers>

using namespace wgpupixel;

namespace {
double distance(double x, double y, int sides, double radius, double rotation) {
    std::vector<std::array<double, 2>> vertices;
    const double radians = rotation * std::numbers::pi / 180;
    for (int i = 0; i < sides; ++i) {
        const double angle = (2 * i - 1) * std::numbers::pi / sides;
        const double px = radius * std::sin(angle), py = radius * std::cos(angle);
        vertices.push_back({px * std::cos(radians) - py * std::sin(radians),
                            px * std::sin(radians) + py * std::cos(radians)});
    }
    double minimum = std::numeric_limits<double>::infinity();
    bool inside = true;
    for (int i = 0; i < sides; ++i) {
        const auto a = vertices[i], b = vertices[(i + 1) % sides];
        const double ex = b[0] - a[0], ey = b[1] - a[1];
        const double dx = x - a[0], dy = y - a[1];
        inside &= ex * dy - ey * dx <= 1e-12;
        const double t = std::clamp((dx * ex + dy * ey) / (ex * ex + ey * ey), 0.0, 1.0);
        minimum = std::min(minimum, std::hypot(dx - t * ex, dy - t * ey));
    }
    return inside ? -minimum : minimum;
}
} // namespace

int main() {
    test::run("polygon independent segment-distance reference", [] {
        auto ctx = Context::create();
        for (auto size : {std::array{9, 7}, std::array{6, 8}, std::array{1, 1}}) {
            auto image = ctx.create_image({size[0], size[1]});
            for (int sides : {3, 4, 7, 1024}) {
                for (float rotation : {0.0f, 45.0f, -90.0f}) {
                    for (float softness : {0.0f, 3.0f}) {
                        auto commands = ctx.create_commands(1);
                        commands.polygon(image, {.sides = sides,
                                                 .color = {0.125f, 0, 0, 0.25f},
                                                 .background = {0, 0.25f, 0.5f, 0.5f},
                                                 .rotation = rotation,
                                                 .softness = softness});
                        ctx.submit_and_wait(commands);
                        std::vector<float> expected;
                        for (int y = 0; y < size[1]; ++y) {
                            for (int x = 0; x < size[0]; ++x) {
                                const double d =
                                    distance(x + 0.5 - size[0] * 0.5, y + 0.5 - size[1] * 0.5,
                                             sides, std::min(size[0], size[1]) * 0.5, rotation);
                                const double t =
                                    std::clamp(0.5 - d / std::max(1.0f, softness), 0.0, 1.0);
                                const float coverage = float(t * t * (3 - 2 * t));
                                expected.insert(expected.end(),
                                                {0.125f * coverage, 0.25f * (1 - coverage),
                                                 0.5f * (1 - coverage), 0.5f - 0.25f * coverage});
                            }
                        }
                        const auto encoded = test::encode(expected);
                        const auto actual = test::read(ctx, image);
                        for (std::size_t i = 0; i < encoded.size(); ++i) {
                            test::check(std::abs(int(actual[i]) - int(encoded[i])) <= 1,
                                        "polygon differs from segment-distance reference");
                        }
                    }
                }
            }
        }
    });
    test::run("polygon HDR and atomic validation", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 3});
        auto commands = ctx.create_commands(2);
        for (int sides : {2, 1025}) {
            test::error(ErrorCode::invalid_argument, "polygon", "sides", [&] {
                commands.polygon(
                    image, {.sides = sides, .color = {0, 0, 0, 1}, .background = {0, 0, 0, 1}});
            });
        }
        test::error(ErrorCode::invalid_argument, "polygon", "rotation", [&] {
            commands.polygon(image, {.sides = 4,
                                     .color = {0, 0, 0, 1},
                                     .background = {0, 0, 0, 1},
                                     .rotation = std::numeric_limits<float>::infinity()});
        });
        test::error(ErrorCode::invalid_argument, "polygon", "softness", [&] {
            commands.polygon(image, {.sides = 4,
                                     .color = {0, 0, 0, 1},
                                     .background = {0, 0, 0, 1},
                                     .rotation = 0,
                                     .softness = -1});
        });
        commands.polygon(image, {.sides = 4,
                                 .color = {0.75f, 0.75f, 0.75f, 0.5f},
                                 .background = {0, 0, 0, 0},
                                 .rotation = 360});
        commands.brightness(image, {.amount = -1.0f});
        ctx.submit_and_wait(commands);
        const auto actual = test::read(ctx, image);
        test::near(actual[16], test::channel(0.5f), 1);
        test::near(actual[19], 128, 1);
    });
    return test::finish();
}
