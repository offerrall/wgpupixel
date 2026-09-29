#include "../test.h"
#include <limits>

using namespace wgpupixel;

namespace {
std::uint32_t hash(std::uint32_t value) {
    value = (value ^ (value >> 16)) * 0x7feb352du;
    value = (value ^ (value >> 15)) * 0x846ca68bu;
    return value ^ (value >> 16);
}
double gradient(int x, int y, std::uint32_t seed, double dx, double dy) {
    const auto key = std::uint32_t(x) * 0x1f123bb5u ^ std::uint32_t(y) * 0x5f356495u ^ seed;
    switch (hash(key) & 7u) {
    case 0:
        return dx;
    case 1:
        return -dx;
    case 2:
        return dy;
    case 3:
        return -dy;
    case 4:
        return (dx + dy) / std::sqrt(2.0);
    case 5:
        return (dx - dy) / std::sqrt(2.0);
    case 6:
        return (-dx + dy) / std::sqrt(2.0);
    default:
        return (-dx - dy) / std::sqrt(2.0);
    }
}
double sample(double x, double y, std::uint32_t seed) {
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const double dx = x - ix, dy = y - iy;
    const auto fade = [](double t) { return t * t * t * (t * (t * 6 - 15) + 10); };
    const double a =
        std::lerp(gradient(ix, iy, seed, dx, dy), gradient(ix + 1, iy, seed, dx - 1, dy), fade(dx));
    const double b = std::lerp(gradient(ix, iy + 1, seed, dx, dy - 1),
                               gradient(ix + 1, iy + 1, seed, dx - 1, dy - 1), fade(dx));
    return std::lerp(a, b, fade(dy));
}
} // namespace

int main() {
    test::run("perlin CPU reference, negative offsets, octaves and alpha", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({7, 5});
        for (auto seed : {0u, 12345u, std::numeric_limits<std::uint32_t>::max()}) {
            for (int octaves : {1, 4, 16}) {
                for (float persistence : {0.0f, 0.5f, 1.0f}) {
                    auto commands = ctx.create_commands(1);
                    commands.perlin(image, {.scale = 4,
                                            .seed = seed,
                                            .octaves = octaves,
                                            .persistence = persistence,
                                            .lacunarity = 2,
                                            .first = {0.125f, 0, 0, 0.25f},
                                            .second = {0, 0.25f, 0.5f, 0.5f},
                                            .offset_x = -1.5f,
                                            .offset_y = 0.5f});
                    ctx.submit_and_wait(commands);
                    std::vector<float> expected;
                    for (int y = 0; y < 5; ++y) {
                        for (int x = 0; x < 7; ++x) {
                            double total = 0, weight = 0, amplitude = 1, frequency = 1;
                            for (int i = 0; i < octaves; ++i) {
                                total += amplitude * sample((x - 1.5) / 4 * frequency,
                                                            (y + 0.5) / 4 * frequency,
                                                            seed + std::uint32_t(i) * 0x9e3779b9u);
                                weight += amplitude;
                                amplitude *= persistence;
                                frequency *= 2;
                            }
                            const float t = float(std::clamp(0.5 + 0.5 * total / weight, 0.0, 1.0));
                            expected.insert(expected.end(), {0.125f * (1 - t), 0.25f * t, 0.5f * t,
                                                             0.25f + 0.25f * t});
                        }
                    }
                    const auto encoded = test::encode(expected);
                    const auto actual = test::read(ctx, image);
                    for (std::size_t i = 0; i < encoded.size(); ++i) {
                        test::check(std::abs(int(actual[i]) - int(encoded[i])) <= 1,
                                    "perlin differs from double precision CPU reference");
                    }
                }
            }
        }
    });
    test::run("perlin seed repeatability and HDR", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({7, 3});
        auto render = [&](std::uint32_t seed) {
            auto commands = ctx.create_commands(2);
            commands.perlin(image, {.scale = 4,
                                    .seed = seed,
                                    .octaves = 3,
                                    .persistence = 0.5f,
                                    .lacunarity = 2,
                                    .first = {0.75f, 0.75f, 0.75f, 0.5f},
                                    .second = {1, 1, 1, 0.5f}});
            commands.brightness(image, {.amount = -1});
            ctx.submit_and_wait(commands);
            return test::read(ctx, image);
        };
        const auto first = render(42);
        test::check(first == render(42), "same seed changed perlin output");
        test::check(first != render(43), "different seeds did not change perlin output");
        // Integer lattice at (0,0) has zero noise, hence midpoint HDR color.
        test::near(first[0], test::channel(0.75f), 1);
        test::near(first[3], 128, 1);
    });
    test::run("perlin rejects unsafe domain and invalid parameters atomically", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 1});
        auto commands = ctx.create_commands(1);
        const Color first{0, 0, 0, 1}, second{1, 1, 1, 1};
        for (float scale : {0.0f, std::numeric_limits<float>::denorm_min(), 1e-10f}) {
            test::error(ErrorCode::invalid_argument, "perlin", "scale", [&] {
                commands.perlin(image, {.scale = scale,
                                        .seed = 0,
                                        .octaves = 1,
                                        .persistence = 0.5f,
                                        .lacunarity = 2,
                                        .first = first,
                                        .second = second});
            });
        }
        test::error(ErrorCode::invalid_argument, "perlin", "scale", [&] {
            commands.perlin(image, {.scale = 1,
                                    .seed = 0,
                                    .octaves = 1,
                                    .persistence = 0.5f,
                                    .lacunarity = 2,
                                    .first = first,
                                    .second = second,
                                    .offset_x = 2000000,
                                    .offset_y = 0});
        });
        test::error(ErrorCode::invalid_argument, "perlin", "octaves", [&] {
            commands.perlin(image, {.scale = 4,
                                    .seed = 0,
                                    .octaves = 17,
                                    .persistence = 0.5f,
                                    .lacunarity = 2,
                                    .first = first,
                                    .second = second});
        });
        test::error(ErrorCode::invalid_argument, "perlin", "persistence", [&] {
            commands.perlin(image, {.scale = 4,
                                    .seed = 0,
                                    .octaves = 1,
                                    .persistence = 2,
                                    .lacunarity = 2,
                                    .first = first,
                                    .second = second});
        });
        test::error(ErrorCode::invalid_argument, "perlin", "lacunarity", [&] {
            commands.perlin(image, {.scale = 4,
                                    .seed = 0,
                                    .octaves = 1,
                                    .persistence = 0.5f,
                                    .lacunarity = 0.5f,
                                    .first = first,
                                    .second = second});
        });
        auto single = ctx.create_image({1, 1});
        test::error(ErrorCode::invalid_argument, "perlin", "lacunarity", [&] {
            commands.perlin(single, {.scale = 4,
                                     .seed = 0,
                                     .octaves = 3,
                                     .persistence = 0.5f,
                                     .lacunarity = std::numeric_limits<float>::max(),
                                     .first = first,
                                     .second = second});
        });
        commands.perlin(image, {.scale = 4,
                                .seed = 0,
                                .octaves = 1,
                                .persistence = 0.5f,
                                .lacunarity = 2,
                                .first = first,
                                .second = second});
        ctx.submit_and_wait(commands);
    });
    return test::finish();
}
