#include "../geometry.h"
#include <limits>

using namespace wgpupixel;
using geometry::Pixels;

int main() {
    test::run("offset wraps, repeats edges, mirrors or clears exactly", [] {
        auto ctx = Context::create();
        const int sw = 7, sh = 5;
        auto source = ctx.create_image({sw, sh});
        const auto pixels = geometry::pattern(sw, sh, 1);
        geometry::upload(ctx, source, pixels);
        constexpr auto low = std::numeric_limits<std::int32_t>::min();
        constexpr auto high = std::numeric_limits<std::int32_t>::max();
        for (const auto size : {std::array{7, 5}, std::array{4, 9}, std::array{1, 1}}) {
            auto destination = ctx.create_image({size[0], size[1]});
            for (auto edge :
                 {EdgeMode::transparent, EdgeMode::clamp, EdgeMode::repeat, EdgeMode::mirror}) {
                for (auto offset : {Position{0, 0}, Position{3, -2}, Position{-15, 11},
                                    Position{low, 4}, Position{high, low}, Position{-1, high}}) {
                    auto cmd = ctx.create_commands(2);
                    cmd.fill(destination, {.color = {9, 9, 9, 1}});
                    cmd.offset(source, destination, {.offset = offset, .edge = edge});
                    ctx.submit_and_wait(cmd);
                    const auto actual = geometry::download(ctx, destination);
                    for (int y = 0; y < size[1]; ++y) {
                        for (int x = 0; x < size[0]; ++x) {
                            const int sx = geometry::edge_index(x - (long long)offset.x, sw, edge);
                            const int sy = geometry::edge_index(y - (long long)offset.y, sh, edge);
                            for (int c = 0; c < 4; ++c) {
                                const float expected =
                                    sx < 0 || sy < 0 ? 0 : pixels[(sy * sw + sx) * 4 + c];
                                test::check(actual[(y * size[0] + x) * 4 + c] == expected,
                                            "offset differs, edge " +
                                                std::to_string(std::to_underlying(edge)));
                            }
                        }
                    }
                }
            }
            ctx.destroy(destination);
        }
    });

    test::run("offset honors mask and region and validates", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({5, 4});
        auto full = ctx.create_image({5, 4});
        auto partial = ctx.create_image({5, 4});
        auto mask = ctx.create_mask({5, 4});
        geometry::upload(ctx, source, geometry::pattern(5, 4, 2));
        std::vector<std::uint8_t> coverage(20);
        for (std::size_t i = 0; i < coverage.size(); ++i) {
            coverage[i] = std::uint8_t(i * 13);
        }
        geometry::upload(ctx, mask, coverage);
        auto cmd = ctx.create_commands(3);
        cmd.fill(partial, {.color = {0.3f, 0, 0, 0.3f}});
        cmd.offset(source, full, {.offset = {2, 3}});
        cmd.offset(source, partial, {.offset = {2, 3}, .mask = &mask, .region = Rect{-1, 1, 4, 2}});
        ctx.submit_and_wait(cmd);
        const auto after = geometry::download(ctx, full);
        const auto actual = geometry::download(ctx, partial);
        const std::array<float, 4> before{0.3f, 0, 0, 0.3f};
        for (int i = 0; i < 20; ++i) {
            const int x = i % 5, y = i / 5;
            const float m = x < 3 && y >= 1 && y < 3 ? coverage[i] / 255.0f : 0;
            for (int c = 0; c < 4; ++c) {
                test::near(actual[i * 4 + c], before[c] + (after[i * 4 + c] - before[c]) * m,
                           1e-6f);
            }
        }
        auto invalid = ctx.create_commands(1);
        test::error(ErrorCode::invalid_argument, "offset", "edge",
                    [&] { invalid.offset(source, full, {.edge = static_cast<EdgeMode>(4)}); });
        test::error(ErrorCode::invalid_argument, "offset", "destination",
                    [&] { invalid.offset(source, source, {}); });
        invalid.offset(source, full, {.offset = {1, 1}});
        ctx.submit_and_wait(invalid);
    });
    return test::finish();
}
