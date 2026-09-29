#include "../geometry.h"
#include <limits>

using namespace wgpupixel;
using geometry::Pixels;

static_assert(Homography{}.map({3, -2}) == Point{3, -2});
static_assert(Homography::from(Affine::translate(4, 5)).map({1, 2}) == Point{5, 7});
// The right operand applies first, as for Affine.
static_assert((Homography::from(Affine::translate(1, 2)) * Homography::from(Affine::scale(2)))
                  .map({1, 1}) == Point{3, 4});
static_assert(Homography{{1, 0, 0, 0, 1, 0, 0.5, 0, 1}}.map({2, 4}) == Point{1, 2});
static_assert(inverse(Homography::from(Affine::scale(2, 4)))->m ==
              Homography::from(Affine::scale(0.5f, 0.25f)).m);
static_assert(!inverse(Homography{{1, 2, 0, 2, 4, 0, 0, 0, 1}}));

namespace {
Pixels run(Context& ctx, const Image& source, const Image& destination,
           const PerspectiveOptions& options) {
    auto cmd = ctx.create_commands(32);
    cmd.fill(destination, {.color = {9, 9, 9, 1}});
    cmd.perspective(source, destination, test::sized(ctx, source.size(), destination.size(), options));
    ctx.submit_and_wait(cmd);
    return geometry::download(ctx, destination);
}
} // namespace

int main() {
    test::run("perspective_matrix maps the source corners onto the quadrilateral", [] {
        const std::array<Point, 4> corners{Point{3, 1}, Point{17.5f, 4}, Point{14, 12},
                                           Point{1, 9.25f}};
        const auto h = perspective_matrix({20, 10}, corners);
        test::check(h.has_value(), "convex quadrilateral must produce a matrix");
        const std::array<Point, 4> source{Point{0, 0}, Point{20, 0}, Point{20, 10}, Point{0, 10}};
        for (int i = 0; i < 4; ++i) {
            const auto p = h->map(source[i]);
            test::near(p.x, corners[i].x, 1e-4f);
            test::near(p.y, corners[i].y, 1e-4f);
        }
        // Straight lines stay straight: the source center lands on both diagonals'
        // projective intersection, not on the average of the corners.
        const auto center = h->map({10, 5});
        const auto cross = [](Point a, Point b, Point c) {
            return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        };
        test::near(cross(corners[0], corners[2], center), 0, 1e-3f);
        test::near(cross(corners[1], corners[3], center), 0, 1e-3f);
        const auto back = inverse(*h);
        test::check(back.has_value(), "perspective matrix must invert");
        const auto round = back->map(h->map({7, 3}));
        test::near(round.x, 7, 1e-4f);
        test::near(round.y, 3, 1e-4f);
        // Rectangle corners give the scaling affine; a parallelogram gives an affine.
        const auto scale =
            perspective_matrix({20, 10}, {Point{0, 0}, Point{40, 0}, Point{40, 5}, Point{0, 5}});
        test::check(*scale == Homography::from(Affine::scale(2, 0.5f)), "rectangle is a scale");
        // Mirrored winding is accepted and flips the image.
        const auto mirrored =
            perspective_matrix({20, 10}, {Point{20, 0}, Point{0, 0}, Point{0, 10}, Point{20, 10}});
        test::check(mirrored.has_value() && mirrored->map({0, 0}) == Point{20, 0}, "mirror");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        for (const auto& invalid :
             {std::array{Point{0, 0}, Point{10, 0}, Point{0, 10}, Point{10, 10}}, // bow tie
              std::array{Point{0, 0}, Point{10, 0}, Point{2, 2}, Point{0, 10}},   // concave
              std::array{Point{0, 0}, Point{5, 0}, Point{10, 0}, Point{0, 10}},   // degenerate
              std::array{Point{0, 0}, Point{nan, 0}, Point{10, 10}, Point{0, 10}}}) {
            test::check(!perspective_matrix({20, 10}, invalid), "invalid corners accepted");
        }
        test::check(!perspective_matrix({0, 10}, corners), "empty source accepted");
    });

    test::run("rectangle and parallelogram corners equal the affine transform", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({9, 7});
        auto destination = ctx.create_image({12, 10});
        auto affine = ctx.create_image({12, 10});
        const auto pixels = geometry::pattern(9, 7, 4);
        geometry::upload(ctx, source, pixels);
        const Affine matrix{0.9f, 0.25f, -0.4f, 1.1f, 3.5f, 0.75f};
        std::array<Point, 4> corners{};
        const std::array<Point, 4> rectangle{Point{0, 0}, Point{9, 0}, Point{9, 7}, Point{0, 7}};
        for (int i = 0; i < 4; ++i) {
            corners[i] = matrix.map(rectangle[i]);
        }
        for (auto filter : {ResizeFilter::bilinear, ResizeFilter::bicubic, ResizeFilter::area}) {
            const auto actual =
                run(ctx, source, destination, {.corners = corners, .filter = filter});
            auto cmd = ctx.create_commands(1);
            cmd.transform(source, affine, {.matrix = matrix, .filter = filter});
            ctx.submit_and_wait(cmd);
            geometry::expect_near(actual, geometry::download(ctx, affine), 2e-4f, "parallelogram");
        }
        const auto identity = run(ctx, source, destination, {.corners = rectangle});
        for (int y = 0; y < 10; ++y) {
            for (int x = 0; x < 12; ++x) {
                for (int c = 0; c < 4; ++c) {
                    const float expected = x < 9 && y < 7 ? pixels[(y * 9 + x) * 4 + c] : 0;
                    test::near(identity[(y * 12 + x) * 4 + c], expected, 1e-5f);
                }
            }
        }
    });

    test::run("true perspective matches the CPU footprint reference for every edge", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({16, 12});
        auto destination = ctx.create_image({20, 14});
        const auto pixels = geometry::pattern(16, 12, 5);
        geometry::upload(ctx, source, pixels);
        // A strongly converging plane: the far edge is reduced about eightfold.
        for (const auto& corners :
             {std::array{Point{8.1f, 1.3f}, Point{11.2f, 0.9f}, Point{19.5f, 13},
                         Point{0.5f, 12.7f}},
              std::array{Point{2, 2}, Point{18, 0.5f}, Point{16, 13.5f}, Point{3.5f, 10}},
              std::array{Point{19, 2}, Point{1, 1}, Point{4, 13}, Point{17, 12}}}) {
            const auto g = inverse(*perspective_matrix({16, 12}, corners))->m;
            for (auto filter : {ResizeFilter::nearest, ResizeFilter::bilinear,
                                ResizeFilter::bicubic, ResizeFilter::lanczos, ResizeFilter::area}) {
                for (auto edge :
                     {EdgeMode::transparent, EdgeMode::clamp, EdgeMode::repeat, EdgeMode::mirror}) {
                    const auto actual = run(ctx, source, destination,
                                            {.corners = corners, .filter = filter, .edge = edge});
                    const auto expected =
                        geometry::transformed(pixels, 16, 12, 20, 14, g, filter, edge);
                    std::size_t different = 0;
                    for (std::size_t i = 0; i < actual.size(); ++i) {
                        const auto pixel = i / 4;
                        if (filter != ResizeFilter::nearest &&
                            geometry::footprint_length(g, pixel % 20 + 0.5, pixel / 20 + 0.5) >
                                geometry::direct_limit(filter, edge)) {
                            continue; // Sampled on the pyramid; checked separately.
                        }
                        // Nearest may pick the neighbor when a center lands on a pixel
                        // boundary within float rounding. Exact ties are avoided above.
                        const float tolerance = filter == ResizeFilter::nearest ? 1e-5f : 5e-4f;
                        different += !(std::abs(actual[i] - expected[i]) <= tolerance);
                    }
                    test::check(different <= (filter == ResizeFilter::nearest ? 8u : 0u),
                                "perspective differs in " + std::to_string(different) +
                                    " components, filter " +
                                    std::to_string(std::to_underlying(filter)) + ", edge " +
                                    std::to_string(std::to_underlying(edge)));
                }
            }
        }
    });

    test::run("the far side of a vanishing plane is filtered, not aliased", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({128, 128});
        auto destination = ctx.create_image({64, 64});
        Pixels board;
        for (int y = 0; y < 128; ++y) {
            for (int x = 0; x < 128; ++x) {
                const float v = float((x + y) % 2);
                board.insert(board.end(), {v, v, v, 1});
            }
        }
        geometry::upload(ctx, source, board);
        const std::array corners{Point{28, 2}, Point{36, 2}, Point{64, 64}, Point{0, 64}};
        const auto actual =
            run(ctx, source, destination,
                {.corners = corners, .filter = ResizeFilter::bilinear, .edge = EdgeMode::repeat});
        // Rows near the horizon reduce 8-16x vertically; they must converge to gray.
        for (int y = 2; y < 20; ++y) {
            for (int x = 0; x < 64; ++x) {
                const auto i = std::size_t(y * 64 + x) * 4;
                if (actual[i + 3] > 0.999f) {
                    test::near(actual[i], 0.5f, 0.1f);
                }
            }
        }
    });

    test::run("pixels beyond the horizon and outside the quad stay transparent", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({8, 8});
        auto destination = ctx.create_image({16, 16});
        auto cmd = ctx.create_commands(1);
        cmd.fill(source, {.color = {0.2f, 0.4f, 0.6f, 1}});
        ctx.submit_and_wait(cmd);
        const std::array corners{Point{7, 4}, Point{9, 4}, Point{16, 16}, Point{0, 16}};
        for (auto edge : {EdgeMode::transparent, EdgeMode::clamp}) {
            const auto actual =
                run(ctx, source, destination,
                    {.corners = corners, .filter = ResizeFilter::lanczos, .edge = edge});
            for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 16; ++x) {
                    const auto i = std::size_t(y * 16 + x) * 4;
                    // The side edges meet at y = 16/7 (the horizon); every edge mode
                    // leaves the sky above it empty.
                    if (y < 2) {
                        test::check(actual[i + 3] == 0, "sky must be transparent");
                    }
                    for (int c = 0; c < 4; ++c) {
                        test::check(std::isfinite(actual[i + c]), "values must be finite");
                    }
                    if (edge == EdgeMode::clamp && actual[i + 3] > 0) {
                        test::near(actual[i] / actual[i + 3], 0.2f, 1e-3f);
                    }
                }
            }
        }
    });

    test::run("perspective mask and region, and invalid arguments", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({6, 6});
        auto full = ctx.create_image({6, 6});
        auto partial = ctx.create_image({6, 6});
        auto mask = ctx.create_mask({6, 6});
        geometry::upload(ctx, source, geometry::pattern(6, 6, 6));
        std::vector<std::uint8_t> coverage(36);
        for (std::size_t i = 0; i < coverage.size(); ++i) {
            coverage[i] = std::uint8_t((i * 91) % 256);
        }
        geometry::upload(ctx, mask, coverage);
        const std::array corners{Point{1, 0}, Point{6, 1}, Point{5, 6}, Point{0, 5}};
        auto cmd = ctx.create_commands(4);
        cmd.fill(partial, {.color = {0, 0, 0.5f, 0.5f}});
        cmd.perspective(source, full, {.corners = corners, .filter = ResizeFilter::bicubic});
        cmd.perspective(source, partial,
                        {.corners = corners,
                         .filter = ResizeFilter::bicubic,
                         .mask = &mask,
                         .region = Rect{2, 1, 10, 3}});
        ctx.submit_and_wait(cmd);
        const auto after = geometry::download(ctx, full);
        const auto actual = geometry::download(ctx, partial);
        const std::array<float, 4> before{0, 0, 0.5f, 0.5f};
        for (int y = 0; y < 6; ++y) {
            for (int x = 0; x < 6; ++x) {
                const float m = x >= 2 && y >= 1 && y < 4 ? coverage[y * 6 + x] / 255.0f : 0;
                for (int c = 0; c < 4; ++c) {
                    const auto i = std::size_t(y * 6 + x) * 4 + c;
                    test::near(actual[i], before[c] + (after[i] - before[c]) * m, 1e-5f);
                }
            }
        }
        auto invalid = ctx.create_commands(1);
        test::error(ErrorCode::invalid_argument, "perspective", "corners", [&] {
            invalid.perspective(source, full,
                                {.corners = {Point{0, 0}, Point{6, 0}, Point{0, 6}, Point{6, 6}}});
        });
        test::error(ErrorCode::invalid_argument, "perspective", "filter", [&] {
            invalid.perspective(source, full,
                                {.corners = corners, .filter = static_cast<ResizeFilter>(9)});
        });
        test::error(ErrorCode::invalid_argument, "perspective", "edge", [&] {
            invalid.perspective(source, full,
                                {.corners = corners, .edge = static_cast<EdgeMode>(9)});
        });
        test::error(ErrorCode::invalid_argument, "perspective", "destination",
                    [&] { invalid.perspective(source, source, {.corners = corners}); });
        invalid.perspective(source, full, {.corners = corners});
        ctx.submit_and_wait(invalid);
    });
    return test::finish();
}
