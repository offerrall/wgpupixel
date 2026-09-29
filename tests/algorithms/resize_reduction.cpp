#include "../geometry.h"
#include <limits>

using namespace wgpupixel;
using geometry::Pixels;

namespace {
Pixels run(Context& ctx, const Pixels& pixels, int sw, int sh, int dw, int dh,
           ResizeFilter filter) {
    auto source = ctx.create_image({sw, sh});
    auto destination = ctx.create_image({dw, dh});
    geometry::upload(ctx, source, pixels);
    auto cmd = ctx.create_commands(2);
    cmd.resize(source, destination, {.filter = filter});
    ctx.submit_and_wait(cmd);
    auto result = geometry::download(ctx, destination);
    ctx.destroy(source);
    ctx.destroy(destination);
    return result;
}

// Independent box-overlap reference: destination pixel x covers the source interval
// [x * sw / dw, (x + 1) * sw / dw); each source pixel weighs by its covered length.
Pixels area(const Pixels& pixels, int sw, int sh, int dw, int dh) {
    const auto weights = [](int d, int source, int destination) {
        std::vector<double> w(source);
        const double low = double(d) * source / destination;
        const double high = double(d + 1) * source / destination;
        for (int i = 0; i < source; ++i) {
            w[i] = std::max(0.0, std::min(high, i + 1.0) - std::max(low, double(i)));
        }
        return w;
    };
    Pixels result;
    for (int y = 0; y < dh; ++y) {
        const auto wy = weights(y, sh, dh);
        for (int x = 0; x < dw; ++x) {
            const auto wx = weights(x, sw, dw);
            std::array<double, 4> sum{};
            double total = 0;
            for (int j = 0; j < sh; ++j) {
                for (int i = 0; i < sw; ++i) {
                    total += wx[i] * wy[j];
                    for (int c = 0; c < 4; ++c) {
                        sum[c] += wx[i] * wy[j] * pixels[(j * sw + i) * 4 + c];
                    }
                }
            }
            for (int c = 0; c < 4; ++c) {
                result.push_back(float(sum[c] / total));
            }
        }
    }
    return result;
}
} // namespace

int main() {
    test::run("area averages exact pixel coverage when reducing and enlarging", [] {
        auto ctx = Context::create();
        const auto pixels = geometry::pattern(8, 6);
        const auto halved = run(ctx, pixels, 8, 6, 4, 3, ResizeFilter::area);
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 4; ++x) {
                for (int c = 0; c < 4; ++c) {
                    float block = 0;
                    for (int j = 0; j < 2; ++j) {
                        for (int i = 0; i < 2; ++i) {
                            block += pixels[((2 * y + j) * 8 + 2 * x + i) * 4 + c] / 4;
                        }
                    }
                    test::near(halved[(y * 4 + x) * 4 + c], block, 1e-5f);
                }
            }
        }
        for (const auto size :
             {std::array{7, 5, 3, 2}, std::array{7, 5, 5, 4}, std::array{3, 2, 7, 5},
              std::array{5, 3, 13, 3}, std::array{9, 1, 2, 1}}) {
            const auto source = geometry::pattern(size[0], size[1], 1);
            geometry::expect_near(
                run(ctx, source, size[0], size[1], size[2], size[3], ResizeFilter::area),
                area(source, size[0], size[1], size[2], size[3]), 2e-5f, "area");
        }
    });

    test::run("reductions widen every interpolating filter by the reduction factor", [] {
        auto ctx = Context::create();
        const auto pixels = geometry::pattern(23, 17, 2);
        for (const auto size : {std::array{7, 5}, std::array{11, 17}, std::array{3, 2},
                                std::array{23, 4}, std::array{30, 6}}) {
            const geometry::Matrix g{23.0 / size[0], 0, 0, 0, 17.0 / size[1], 0, 0, 0, 1};
            for (auto filter : {ResizeFilter::bilinear, ResizeFilter::bicubic,
                                ResizeFilter::lanczos, ResizeFilter::area}) {
                geometry::expect_near(run(ctx, pixels, 23, 17, size[0], size[1], filter),
                                      geometry::transformed(pixels, 23, 17, size[0], size[1], g,
                                                            filter, EdgeMode::clamp, 4, 1e30),
                                      2e-4f, "resize reduction");
            }
        }
    });

    test::run("reduced checkerboards converge to gray; nearest keeps point samples", [] {
        auto ctx = Context::create();
        Pixels board;
        for (int y = 0; y < 96; ++y) {
            for (int x = 0; x < 96; ++x) {
                const float v = float((x + y) % 2);
                board.insert(board.end(), {v, v, v, 1});
            }
        }
        for (const auto size : {std::array{24, 24}, std::array{17, 13}, std::array{5, 40}}) {
            for (auto filter : {ResizeFilter::bilinear, ResizeFilter::bicubic,
                                ResizeFilter::lanczos, ResizeFilter::area}) {
                const auto actual = run(ctx, board, 96, 96, size[0], size[1], filter);
                for (std::size_t i = 0; i < actual.size(); i += 4) {
                    test::near(actual[i], 0.5f, 0.06f);
                    test::near(actual[i + 3], 1, 1e-5f);
                }
            }
            const auto nearest = run(ctx, board, 96, 96, size[0], size[1], ResizeFilter::nearest);
            for (std::size_t i = 0; i < nearest.size(); i += 4) {
                test::check(nearest[i] == 0 || nearest[i] == 1, "nearest must not filter");
            }
        }
    });

    test::run("area keeps enlargement pixel-exact at integer factors and validates", [] {
        auto ctx = Context::create();
        const auto pixels = geometry::pattern(3, 2, 3);
        const auto large = run(ctx, pixels, 3, 2, 9, 6, ResizeFilter::area);
        for (int y = 0; y < 6; ++y) {
            for (int x = 0; x < 9; ++x) {
                for (int c = 0; c < 4; ++c) {
                    test::near(large[(y * 9 + x) * 4 + c], pixels[((y / 3) * 3 + x / 3) * 4 + c],
                               1e-6f);
                }
            }
        }
        auto source = ctx.create_image({2, 2});
        auto destination = ctx.create_image({1, 1});
        auto cmd = ctx.create_commands(1);
        test::error(ErrorCode::invalid_argument, "resize", "filter", [&] {
            cmd.resize(source, destination, {.filter = static_cast<ResizeFilter>(5)});
        });
        test::error(ErrorCode::invalid_argument, "rotate", "filter", [&] {
            cmd.rotate(source, destination, {.degrees = 1, .filter = ResizeFilter::area});
        });
    });
    return test::finish();
}
