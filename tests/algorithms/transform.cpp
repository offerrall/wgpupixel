#include "../geometry.h"
#include "../resampling.h"
#include <limits>
#include <utility>

using namespace wgpupixel;
using geometry::Pixels;

namespace {
constexpr std::array filters{ResizeFilter::nearest, ResizeFilter::bilinear, ResizeFilter::bicubic,
                             ResizeFilter::lanczos, ResizeFilter::area};
constexpr std::array edges{EdgeMode::transparent, EdgeMode::clamp, EdgeMode::repeat,
                           EdgeMode::mirror};

std::string label(ResizeFilter filter, EdgeMode edge) {
    return "filter " + std::to_string(std::to_underlying(filter)) + ", edge " +
           std::to_string(std::to_underlying(edge));
}

Pixels run(Context& ctx, const Image& source, const Image& destination,
           const TransformOptions& options) {
    auto cmd = ctx.create_commands(32);
    cmd.fill(destination, {.color = {9, 9, 9, 1}});
    cmd.transform(source, destination, test::sized(ctx, source.size(), destination.size(), options));
    ctx.submit_and_wait(cmd);
    return geometry::download(ctx, destination);
}

Pixels checkerboard(int size) {
    Pixels pixels;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float v = float((x + y) % 2);
            pixels.insert(pixels.end(), {v, v, v, 1});
        }
    }
    return pixels;
}
} // namespace

int main() {
    test::run("identity and whole-pixel translation are exact for every filter and edge", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({7, 5});
        auto destination = ctx.create_image({7, 5});
        const auto pixels = geometry::pattern(7, 5);
        geometry::upload(ctx, source, pixels);
        for (auto filter : filters) {
            for (auto edge : edges) {
                const auto actual = run(ctx, source, destination, {.filter = filter, .edge = edge});
                geometry::expect_near(actual, pixels, 1e-6f, "identity " + label(filter, edge));
                const auto moved =
                    run(ctx, source, destination,
                        {.matrix = Affine::translate(2, -1), .filter = filter, .edge = edge});
                for (int y = 0; y < 5; ++y) {
                    for (int x = 0; x < 7; ++x) {
                        const int sx = geometry::edge_index(x - 2, 7, edge);
                        const int sy = geometry::edge_index(y + 1, 5, edge);
                        for (int c = 0; c < 4; ++c) {
                            const float expected =
                                sx < 0 || sy < 0 ? 0 : pixels[(sy * 7 + sx) * 4 + c];
                            test::near(moved[(y * 7 + x) * 4 + c], expected, 1e-6f);
                        }
                    }
                }
            }
        }
    });

    test::run("rotation, scaling, shear and mirroring match the CPU footprint reference", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({11, 9});
        auto destination = ctx.create_image({13, 10});
        const auto pixels = geometry::pattern(11, 9, 3);
        geometry::upload(ctx, source, pixels);
        const std::array matrices{Affine::translate(1.25f, 0.5f) * Affine::rotate(31, {5.5f, 4.5f}),
                                  Affine::scale(1.7f, 1.3f) * Affine::translate(-1.5f, -0.25f),
                                  Affine::translate(3, 2) * Affine::scale(0.37f),
                                  Affine::translate(6, 1) * Affine::rotate(20) *
                                      Affine::scale(0.3f, 1.4f),
                                  Affine{1, 0.35f, -0.6f, 0.8f, 4, -1},
                                  Affine::translate(12, 0) * Affine::scale(-1, 1),
                                  Affine::rotate(90, {5.5f, 4.5f}) * Affine::scale(0.5f, 2)};
        for (const auto& matrix : matrices) {
            const auto g = geometry::inverse_affine(matrix);
            for (auto filter : filters) {
                for (auto edge : edges) {
                    const auto actual = run(ctx, source, destination,
                                            {.matrix = matrix, .filter = filter, .edge = edge});
                    const auto expected =
                        geometry::transformed(pixels, 11, 9, 13, 10, g, filter, edge);
                    geometry::expect_near(actual, expected, 3e-4f, label(filter, edge));
                }
            }
        }
    });

    test::run("rotation matches the independent zero-extended reconstruction", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({5, 3});
        auto destination = ctx.create_image({7, 5});
        const auto pixels = geometry::pattern(5, 3, 1);
        geometry::upload(ctx, source, pixels);
        // rotate's convention (centers of both images) expressed as an affine transform.
        const auto matrix =
            Affine::translate(3.5f, 2.5f) * Affine::rotate(31) * Affine::translate(-2.5f, -1.5f);
        const auto inverse_matrix = *inverse(matrix);
        for (auto filter : {ResizeFilter::bilinear, ResizeFilter::bicubic, ResizeFilter::lanczos}) {
            const auto actual = run(ctx, source, destination, {.matrix = matrix, .filter = filter});
            Pixels expected;
            for (int y = 0; y < 5; ++y) {
                for (int x = 0; x < 7; ++x) {
                    const auto q = inverse_matrix.map({x + 0.5f, y + 0.5f});
                    const auto value =
                        test::transparent_sample(pixels, 5, 3, q.x - 0.5, q.y - 0.5, filter);
                    expected.insert(expected.end(), value.begin(), value.end());
                }
            }
            geometry::expect_near(actual, expected, 3e-4f, "reconstruction");
            // The rotate operation shares the convention and the result.
            auto rotated = ctx.create_image({7, 5});
            auto cmd = ctx.create_commands(1);
            cmd.rotate(source, rotated, {.degrees = 31, .filter = filter});
            ctx.submit_and_wait(cmd);
            geometry::expect_near(geometry::download(ctx, rotated), actual, 3e-4f, "rotate");
            ctx.destroy(rotated);
        }
    });

    test::run("minification averages a one-pixel checkerboard instead of aliasing", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({64, 64});
        auto destination = ctx.create_image({16, 16});
        geometry::upload(ctx, source, checkerboard(64));
        for (const auto& matrix :
             {Affine::scale(0.25f), Affine::scale(0.3f), Affine::rotate(30) * Affine::scale(0.3f),
              Affine::scale(0.2f, 0.5f), Affine{0.2f, 0.1f, 0.05f, 0.3f, 0.5f, 0.25f}}) {
            for (auto filter : filters) {
                const auto actual =
                    run(ctx, source, destination,
                        {.matrix = matrix, .filter = filter, .edge = EdgeMode::repeat});
                float low = 1, high = 0;
                for (std::size_t i = 0; i < actual.size(); i += 4) {
                    low = std::min(low, actual[i]);
                    high = std::max(high, actual[i]);
                    test::near(actual[i + 3], 1, 1e-5f);
                }
                if (filter == ResizeFilter::nearest) {
                    // Point samples keep only black or white: the aliasing filters remove.
                    for (std::size_t i = 0; i < actual.size(); i += 4) {
                        test::check(actual[i] == 0 || actual[i] == 1, "nearest must point-sample");
                    }
                } else {
                    // A box (area) keeps the most residual aliasing: a footprint of about
                    // ten pixels whose partial edge pixels are unbalanced.
                    const float range = filter == ResizeFilter::area ? 0.2f : 0.08f;
                    test::check(high - low < range && std::abs(0.5f * (high + low) - 0.5f) < 0.04f,
                                "filtered reduction aliases: range " + std::to_string(low) + ".." +
                                    std::to_string(high) + ", filter " +
                                    std::to_string(std::to_underlying(filter)));
                }
            }
        }
    });

    test::run("reductions beyond the kernel limit still average the image", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({512, 512});
        auto destination = ctx.create_image({2, 2});
        geometry::upload(ctx, source, checkerboard(512));
        for (auto filter : {ResizeFilter::bilinear, ResizeFilter::lanczos, ResizeFilter::area}) {
            const auto actual = run(
                ctx, source, destination,
                {.matrix = Affine::scale(1.0f / 256), .filter = filter, .edge = EdgeMode::repeat});
            for (std::size_t i = 0; i < actual.size(); i += 4) {
                test::near(actual[i], 0.5f, 0.05f);
                test::near(actual[i + 3], 1, 1e-5f);
            }
        }
    });

    test::run("transparent edges antialias a layer border and keep straight color", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({16, 16});
        auto destination = ctx.create_image({24, 24});
        auto cmd = ctx.create_commands(2);
        cmd.fill(source, {.color = {0.5f, 0.25f, 0.125f, 0.5f}});
        ctx.submit_and_wait(cmd);
        for (auto filter : {ResizeFilter::bilinear, ResizeFilter::area}) {
            const auto actual = run(ctx, source, destination,
                                    {.matrix = Affine::rotate(17, {12, 12}) *
                                               Affine::translate(4.3f, 3.6f) * Affine::scale(0.9f),
                                     .filter = filter});
            int partial = 0;
            for (std::size_t i = 0; i < actual.size(); i += 4) {
                const float alpha = actual[i + 3];
                test::check(alpha >= -1e-6f && alpha <= 0.5f + 1e-5f, "alpha out of range");
                if (alpha > 1e-4f) {
                    test::near(actual[i] / alpha, 1, 1e-3f);
                    test::near(actual[i + 1] / alpha, 0.5f, 1e-3f);
                    test::near(actual[i + 2] / alpha, 0.25f, 1e-3f);
                }
                partial += alpha > 0.01f && alpha < 0.49f;
            }
            test::check(partial > 10, "border must have partial coverage");
        }
    });

    test::run("mask and region blend with the initialized destination", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({6, 5});
        auto full = ctx.create_image({6, 5});
        auto partial = ctx.create_image({6, 5});
        auto mask = ctx.create_mask({6, 5});
        const auto pixels = geometry::pattern(6, 5, 2);
        geometry::upload(ctx, source, pixels);
        std::vector<std::uint8_t> coverage(30);
        for (std::size_t i = 0; i < coverage.size(); ++i) {
            coverage[i] = std::uint8_t((i * 53) % 256);
        }
        geometry::upload(ctx, mask, coverage);
        const TransformOptions options{.matrix =
                                           Affine::rotate(25, {3, 2.5f}) * Affine::scale(0.8f),
                                       .filter = ResizeFilter::bicubic,
                                       .edge = EdgeMode::mirror};
        const Rect region{1, 1, 4, 3};
        auto cmd = ctx.create_commands(4);
        cmd.fill(partial, {.color = {0.1f, 0.2f, 0.3f, 0.4f}});
        cmd.transform(source, full, options);
        auto masked = options;
        masked.mask = &mask;
        masked.region = region;
        cmd.transform(source, partial, masked);
        ctx.submit_and_wait(cmd);
        const auto after = geometry::download(ctx, full);
        const auto actual = geometry::download(ctx, partial);
        const std::array<float, 4> before{0.1f, 0.2f, 0.3f, 0.4f};
        for (int y = 0; y < 5; ++y) {
            for (int x = 0; x < 6; ++x) {
                const bool inside = x >= 1 && x < 5 && y >= 1 && y < 4;
                const float m = inside ? coverage[y * 6 + x] / 255.0f : 0;
                for (int c = 0; c < 4; ++c) {
                    const auto i = std::size_t(y * 6 + x) * 4 + c;
                    test::near(actual[i], before[c] + (after[i] - before[c]) * m, 1e-5f);
                }
            }
        }
    });

    test::run("invalid arguments fail atomically", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({2, 2});
        auto destination = ctx.create_image({2, 2});
        auto other = ctx.create_mask({3, 3});
        auto cmd = ctx.create_commands(1);
        const float inf = std::numeric_limits<float>::infinity();
        for (const auto& matrix :
             {Affine::scale(0, 1), Affine{1, 2, 2, 4, 0, 0}, Affine{1, 0, 0, 1, inf, 0},
              Affine::scale(std::numeric_limits<float>::denorm_min())}) {
            test::error(ErrorCode::invalid_argument, "transform", "matrix",
                        [&] { cmd.transform(source, destination, {.matrix = matrix}); });
        }
        test::error(ErrorCode::invalid_argument, "transform", "filter", [&] {
            cmd.transform(source, destination, TransformOptions{.filter = static_cast<ResizeFilter>(5)});
        });
        test::error(ErrorCode::invalid_argument, "transform", "edge", [&] {
            cmd.transform(source, destination, TransformOptions{.edge = static_cast<EdgeMode>(4)});
        });
        test::error(ErrorCode::invalid_argument, "transform", "destination",
                    [&] { cmd.transform(source, source, TransformOptions{}); });
        test::error(ErrorCode::invalid_argument, "transform", "mask",
                    [&] { cmd.transform(source, destination, TransformOptions{.mask = &other}); });
        test::error(ErrorCode::invalid_argument, "transform", "region",
                    [&] { cmd.transform(source, destination, TransformOptions{.region = Rect{0, 0, -1, 1}}); });
        // Nothing was recorded, so the single slot is still available.
        cmd.transform(source, destination, {.matrix = Affine::translate(1, 0)});
        ctx.submit_and_wait(cmd);
    });

    test::run("transform_bounds sizes the output layer", [] {
        using wgpupixel::transform_bounds;
        const Rect layer{0, 0, 30, 20};
        const auto same = transform_bounds(layer, Affine::identity());
        test::check(same.x == 0 && same.y == 0 && same.width == 30 && same.height == 20,
                    "identity bounds");
        const auto quarter = transform_bounds(layer, Affine::rotate(90, {15, 10}));
        test::check(quarter.x == 5 && quarter.y == -5 && quarter.width == 20 &&
                        quarter.height == 30,
                    "exact quarter turn must not gain rows or columns");
        const auto shifted = transform_bounds({2, 3, 4, 5}, Affine::translate(0.5f, -0.25f));
        test::check(shifted.x == 2 && shifted.y == 2 && shifted.width == 5 && shifted.height == 6,
                    "fractional translation covers partial pixels");
        const auto turned = transform_bounds(layer, Affine::rotate(45));
        const double c = std::sqrt(0.5);
        test::check(turned.x == int(std::floor(-20 * c)) && turned.y == 0 &&
                        turned.width == int(std::ceil(30 * c)) - int(std::floor(-20 * c)) &&
                        turned.height == int(std::ceil(50 * c)),
                    "rotated bounds");
        const auto projective = transform_bounds(
            layer, *perspective_matrix(
                       {30, 20}, {Point{1.5f, 2}, Point{40, 0}, Point{35, 30}, Point{-3.2f, 25}}));
        test::check(projective.x == -4 && projective.y == 0 && projective.width == 44 &&
                        projective.height == 30,
                    "perspective bounds are the corner bounds");
        test::error(ErrorCode::invalid_argument, "transform_bounds", "source",
                    [&] { (void)transform_bounds(Rect{0, 0, 0, 1}, Affine{}); });
        test::error(ErrorCode::invalid_argument, "transform_bounds", "matrix", [&] {
            (void)transform_bounds(layer, Affine::scale(std::numeric_limits<float>::max()));
        });
        test::error(ErrorCode::invalid_argument, "transform_bounds", "matrix", [&] {
            (void)transform_bounds(layer, Homography{{1, 0, 0, 0, 1, 0, 0.1, 0, -1}});
        });
        // A matrix and its negation describe the same mapping.
        const auto negated = transform_bounds(layer, Homography{{-1, 0, -2, 0, -1, 0, 0, 0, -1}});
        test::check(negated.x == 2 && negated.y == 0 && negated.width == 30, "negated matrix");
    });
    return test::finish();
}
