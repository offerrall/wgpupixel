#include "../geometry.h"
#include <chrono>
#include <limits>
#include <numbers>

// Regressions from review. Expected values come from closed forms, conservation laws or
// brute-force sums of the documented definitions, never from the shader's formulation.
using namespace wgpupixel;
using geometry::Pixels;

namespace {
float hashed(int x, int y) {
    std::uint32_t h = std::uint32_t(x) * 73856093u ^ std::uint32_t(y) * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return float(h % 1000) / 999.0f;
}

// Opaque gray pattern with fine detail: value in red, fixed green and blue.
Pixels detailed(int width, int height) {
    Pixels pixels;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            pixels.insert(pixels.end(), {hashed(x, y), 0.25f, float((x / 7 + y / 5) % 2), 1});
        }
    }
    return pixels;
}

double radius_of(ResizeFilter filter) {
    return filter == ResizeFilter::bilinear ? 1 : filter == ResizeFilter::bicubic ? 2 : 3;
}

// Brute force of the documented resize: kernel widened by the reduction factor, taps
// at every source pixel center within reach, edge-clamped, normalized. All channels.
std::array<double, 4> resized(const Pixels& pixels, int sw, int sh, int dw, int dh, int x, int y,
                              ResizeFilter filter) {
    const double sx = double(sw) / dw, sy = double(sh) / dh;
    const double cx = (x + 0.5) * sx, cy = (y + 0.5) * sy;
    const double wx = std::max(sx, 1.0), wy = std::max(sy, 1.0), r = radius_of(filter);
    std::array<double, 4> sum{};
    double total = 0;
    for (long j = long(std::floor(cy - r * wy)) - 1; j <= long(cy + r * wy) + 1; ++j) {
        const double ky = geometry::kernel((j + 0.5 - cy) / wy, filter);
        if (ky == 0) {
            continue;
        }
        const long yy = std::clamp<long>(j, 0, sh - 1);
        for (long i = long(std::floor(cx - r * wx)) - 1; i <= long(cx + r * wx) + 1; ++i) {
            const double w = geometry::kernel((i + 0.5 - cx) / wx, filter) * ky;
            total += w;
            const auto* pixel = &pixels[(yy * sw + std::clamp<long>(i, 0, sw - 1)) * 4];
            for (int c = 0; c < 4; ++c) {
                sum[c] += w * pixel[c];
            }
        }
    }
    for (auto& value : sum) {
        value /= total;
    }
    return sum;
}

// Exact box average of source rows/columns [x0, x1) x [y0, y1) with integer bounds.
double block_mean(const Pixels& pixels, int width, int x0, int y0, int x1, int y1, int c) {
    double sum = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            sum += pixels[(y * width + x) * 4 + c];
        }
    }
    return sum / (double(x1 - x0) * (y1 - y0));
}

Pixels transform(Context& ctx, const Image& source, ImageSize size,
                 const TransformOptions& options) {
    auto destination = ctx.create_image(size);
    auto cmd = ctx.create_commands(64);
    cmd.transform(source, destination, test::sized(ctx, source.size(), destination.size(), options));
    ctx.submit_and_wait(cmd);
    auto result = geometry::download(ctx, destination);
    ctx.destroy(destination);
    return result;
}

// Fraction of the unit pixel [x, x + 1) x [y, y + 1) inside the image of the source
// rectangle [0, w] x [0, h] under matrix, by supersampling the destination pixel.
double coverage(const Affine& matrix, int w, int h, int x, int y, int samples) {
    const auto back = *inverse(Homography::from(matrix));
    int inside = 0;
    for (int j = 0; j < samples; ++j) {
        for (int i = 0; i < samples; ++i) {
            const Point p{float(x + (i + 0.5) / samples), float(y + (j + 0.5) / samples)};
            const auto q = back.map(p);
            inside += q.x >= 0 && q.y >= 0 && q.x < w && q.y < h;
        }
    }
    return double(inside) / (double(samples) * samples);
}
} // namespace

int main() {
    test::run("resize reductions keep every source pixel in the footprint", [] {
        auto ctx = Context::create();
        // 512x1, the central 128 pixels white: a 1x1 area average is exactly 1/4.
        Pixels band;
        for (int x = 0; x < 512; ++x) {
            const float v = x >= 192 && x < 320 ? 1.0f : 0.0f;
            band.insert(band.end(), {v, v, v, 1});
        }
        auto source = ctx.create_image({512, 1});
        auto one = ctx.create_image({1, 1});
        geometry::upload(ctx, source, band);
        for (auto filter : {ResizeFilter::bilinear, ResizeFilter::bicubic, ResizeFilter::lanczos,
                            ResizeFilter::area}) {
            auto cmd = ctx.create_commands(2);
            cmd.resize(source, one, test::sized(ctx, source.size(), one.size(), ResizeOptions{.filter = filter}));
            ctx.submit_and_wait(cmd);
            const float expected = filter == ResizeFilter::area
                                       ? 0.25f
                                       : float(resized(band, 512, 1, 1, 1, 0, 0, filter)[0]);
            test::near(geometry::download(ctx, one)[0], expected, 1e-5f);
        }
        test::near(float(resized(band, 512, 1, 1, 1, 0, 0, ResizeFilter::bilinear)[0]), 0.234375f,
                   1e-6f); // Tent of radius 512 over the band: closed form 15/64.
        ctx.destroy(source);
        ctx.destroy(one);
    });

    test::run("very large resize reductions match brute force for every filter", [] {
        auto ctx = Context::create();
        const int sw = 1500, sh = 1000, dw = 3, dh = 2;
        const auto pixels = detailed(sw, sh);
        auto source = ctx.create_image({sw, sh});
        auto destination = ctx.create_image({dw, dh});
        geometry::upload(ctx, source, pixels);
        for (auto filter : {ResizeFilter::bilinear, ResizeFilter::bicubic, ResizeFilter::lanczos,
                            ResizeFilter::area}) {
            auto cmd = ctx.create_commands(2);
            cmd.resize(source, destination, test::sized(ctx, source.size(), destination.size(), ResizeOptions{.filter = filter}));
            ctx.submit_and_wait(cmd);
            const auto actual = geometry::download(ctx, destination);
            for (int y = 0; y < dh; ++y) {
                for (int x = 0; x < dw; ++x) {
                    const auto brute = filter == ResizeFilter::area
                                           ? std::array<double, 4>{}
                                           : resized(pixels, sw, sh, dw, dh, x, y, filter);
                    for (int c = 0; c < 4; ++c) {
                        const double expected = filter == ResizeFilter::area
                                                    ? block_mean(pixels, sw, x * 500, y * 500,
                                                                 (x + 1) * 500, (y + 1) * 500, c)
                                                    : brute[c];
                        test::near(actual[(y * dw + x) * 4 + c], float(expected), 2e-4f);
                    }
                }
            }
        }
        // Masks take the same passes.
        auto mask = ctx.create_mask({sw, sh});
        auto small = ctx.create_mask({dw, dh});
        std::vector<std::uint8_t> bytes(std::size_t(sw) * sh);
        Pixels coverage;
        for (int i = 0; i < sw * sh; ++i) {
            bytes[i] = std::uint8_t(hashed(i % sw, i / sw) * 255);
            const float c = bytes[i] / 255.0f;
            coverage.insert(coverage.end(), {c, c, c, c});
        }
        geometry::upload(ctx, mask, bytes);
        auto cmd = ctx.create_commands(2);
        cmd.resize(mask, small, test::sized(ctx, mask.size(), small.size(), ResizeOptions{.filter = ResizeFilter::area}));
        ctx.submit_and_wait(cmd);
        const auto reduced = geometry::download(ctx, small);
        for (int i = 0; i < dw * dh; ++i) {
            const int x = i % dw, y = i / dw;
            const double mean =
                block_mean(coverage, sw, x * 500, y * 500, (x + 1) * 500, (y + 1) * 500, 3);
            test::check(std::abs(reduced[i] - mean * 255) <= 1, "mask area reduction");
        }
    });

    test::run("large transform reductions average whole blocks and keep edges opaque", [] {
        auto ctx = Context::create();
        const auto pixels = detailed(512, 512);
        auto source = ctx.create_image({512, 512});
        geometry::upload(ctx, source, pixels);
        const auto blocks =
            transform(ctx, source, {2, 2},
                      {.matrix = Affine::scale(1.0f / 256), .filter = ResizeFilter::area});
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < 2; ++x) {
                for (int c = 0; c < 4; ++c) {
                    test::near(blocks[(y * 2 + x) * 4 + c],
                               float(block_mean(pixels, 512, x * 256, y * 256, (x + 1) * 256,
                                                (y + 1) * 256, c)),
                               1e-4f);
                }
            }
        }
        // A 100:1 horizontal squash of an opaque layer: the top and bottom rows must stay
        // fully covered (no blur across the short axis), and detail averages out.
        Pixels board;
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 1000; ++x) {
                const float v = float((x + y) % 2);
                board.insert(board.end(), {v, v, v, 1});
            }
        }
        auto wide = ctx.create_image({1000, 8});
        geometry::upload(ctx, wide, board);
        for (auto filter : {ResizeFilter::bilinear, ResizeFilter::lanczos, ResizeFilter::area}) {
            const auto squashed = transform(ctx, wide, {10, 8},
                                            {.matrix = Affine::scale(0.01f, 1), .filter = filter});
            for (int y = 0; y < 8; ++y) {
                // Kernels wider than a pixel soften (and lanczos rings at) the left and right
                // layer edges.
                for (int x = filter == ResizeFilter::area ? 0 : 3;
                     x < (filter == ResizeFilter::area ? 10 : 7); ++x) {
                    const auto i = std::size_t(y * 10 + x) * 4;
                    test::near(squashed[i + 3], 1, 1e-4f);
                    test::near(squashed[i], 0.5f, 0.02f);
                }
            }
        }
        ctx.destroy(wide);
    });

    test::run("area weighs rotated footprints by their exact overlap", [] {
        auto ctx = Context::create();
        auto white = ctx.create_image({1, 1});
        geometry::upload(ctx, white, {1, 1, 1, 1});
        // A unit square turned 45 degrees about its center overlaps itself by 2 sqrt 2 - 2.
        const auto turned =
            transform(ctx, white, {1, 1},
                      {.matrix = Affine::rotate(45, {0.5f, 0.5f}), .filter = ResizeFilter::area});
        test::near(turned[3], float(2 * std::numbers::sqrt2 - 2), 1e-5f);
        test::near(turned[0], turned[3], 1e-6f);

        // Coverage conservation and supersampled truth for a rotated, scaled layer.
        auto square = ctx.create_image({4, 4});
        auto cmd = ctx.create_commands(1);
        cmd.fill(square, {.color = {1, 1, 1, 1}});
        ctx.submit_and_wait(cmd);
        for (const auto& matrix :
             {Affine::translate(1, 1) * Affine::rotate(30, {2, 2}),
              Affine::translate(3, 2) * Affine::rotate(20) * Affine::scale(2.5f, 1.5f),
              Affine::translate(1.5f, 1) * Affine{0.7f, 0.2f, -0.3f, 0.6f, 0, 0}}) {
            const auto actual =
                transform(ctx, square, {14, 14}, {.matrix = matrix, .filter = ResizeFilter::area});
            double total = 0;
            for (int y = 0; y < 14; ++y) {
                for (int x = 0; x < 14; ++x) {
                    const float alpha = actual[(y * 14 + x) * 4 + 3];
                    total += alpha;
                    test::near(alpha, float(coverage(matrix, 4, 4, x, y, 256)), 4e-3f);
                }
            }
            const double determinant = double(matrix.a) * matrix.d - double(matrix.b) * matrix.c;
            test::near(float(total), float(16 * std::abs(determinant)), 2e-3f);
        }
    });

    test::run("remote clamped positions read the correct edge", [] {
        auto ctx = Context::create();
        auto pair = ctx.create_image({2, 2});
        geometry::upload(ctx, pair, {1, 0, 0, 1, 0, 1, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1});
        for (float distance : {3e4f, 1e7f, 1e8f, 1e20f}) {
            for (auto filter : {ResizeFilter::nearest, ResizeFilter::bilinear,
                                ResizeFilter::bicubic, ResizeFilter::lanczos, ResizeFilter::area}) {
                // Moving the image far left leaves its right column (green) under the
                // destination; far right leaves the left column (red).
                for (const auto& [shift, red] : {std::pair{-distance, 0.0f}, {distance, 1.0f}}) {
                    const auto actual = transform(ctx, pair, {1, 1},
                                                  {.matrix = Affine::translate(shift, 0.5f),
                                                   .filter = filter,
                                                   .edge = EdgeMode::clamp});
                    test::near(actual[0], red, 1e-5f);
                    test::near(actual[1], 1 - red, 1e-5f);
                    test::near(actual[3], 1, 1e-5f);
                }
            }
        }
    });

    test::run("an empty selection handle is rejected by image and mask geometry", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({2, 2});
        auto image_out = ctx.create_image({2, 2});
        auto source = ctx.create_mask({2, 2});
        auto destination = ctx.create_mask({2, 2});
        const Mask empty;
        auto cmd = ctx.create_commands(4);
        const auto rejects = [&](std::string_view name, auto record) {
            test::error(ErrorCode::invalid_resource, name, "mask", record);
        };
        const std::array corners{Point{0, 0}, Point{2, 0}, Point{2, 2}, Point{0, 2}};
        rejects("resize", [&] { cmd.resize(source, destination, {.mask = &empty}); });
        rejects("crop", [&] { cmd.crop(source, destination, {.mask = &empty}); });
        rejects("flip", [&] { cmd.flip(source, destination, {.mask = &empty}); });
        rejects("rotate", [&] { cmd.rotate(source, destination, {.degrees = 3, .mask = &empty}); });
        rejects("offset", [&] { cmd.offset(source, destination, {.mask = &empty}); });
        rejects("transform", [&] { cmd.transform(source, destination, TransformOptions{.mask = &empty}); });
        rejects("perspective", [&] {
            cmd.perspective(source, destination, test::sized(ctx, source.size(), destination.size(), PerspectiveOptions{.corners = corners, .mask = &empty}));
        });
        rejects("transform", [&] { cmd.transform(image, image_out, TransformOptions{.mask = &empty}); });
        rejects("offset", [&] { cmd.offset(image, image_out, {.mask = &empty}); });
        rejects("perspective",
                [&] { cmd.perspective(image, image_out, test::sized(ctx, image.size(), image_out.size(), PerspectiveOptions{.corners = corners, .mask = &empty})); });
    });

    test::run("perspective to the horizon preserves constants and stays bounded", [] {
        auto ctx = Context::create();
        const int size = 2048;
        auto source = ctx.create_image({size, size});
        auto destination = ctx.create_image({640, 360});
        auto cmd = ctx.create_commands(64);
        cmd.fill(source, {.color = {0.3f, 0.2f, 0.1f, 1}});
        ctx.submit_and_wait(cmd);
        // Floors receding to a horizontal horizon (top and bottom edges are parallel), where
        // the side edges meet: y = 20 - 340 * 40 / 600 = -8/3 (above the image, so every
        // pixel shows the floor) and y = 100 - 260 / 15 = 248/3 (inside: the sky above it
        // is empty). Clamp and repeat extend the constant floor up to the horizon.
        const std::array<std::pair<std::array<Point, 4>, double>, 2> floors{
            std::pair{std::array{Point{300, 20}, Point{340, 20}, Point{640, 360}, Point{0, 360}},
                      -8.0 / 3},
            std::pair{std::array{Point{300, 100}, Point{340, 100}, Point{640, 360}, Point{0, 360}},
                      248.0 / 3}};
        const auto start = std::chrono::steady_clock::now();
        for (const auto& [corners, horizon] : floors) {
            for (auto filter :
                 {ResizeFilter::bilinear, ResizeFilter::lanczos, ResizeFilter::area}) {
                for (auto edge : {EdgeMode::clamp, EdgeMode::repeat}) {
                    cmd.perspective(source, destination, test::sized(ctx, source.size(), destination.size(), PerspectiveOptions{.corners = corners, .filter = filter, .edge = edge}));
                    ctx.submit_and_wait(cmd);
                    const auto actual = geometry::download(ctx, destination);
                    for (std::size_t i = 0; i < actual.size(); i += 4) {
                        const double y = double(i / 4 / 640) + 0.5;
                        const float alpha = y > horizon ? 1.0f : 0.0f;
                        test::near(actual[i + 3], alpha, 1e-3f);
                        test::near(actual[i], 0.3f * alpha, 1e-3f);
                    }
                }
            }
        }
        // Large reductions of every kind in one batch finish promptly.
        auto one = ctx.create_image({1, 1});
        cmd.resize(source, one, test::sized(ctx, source.size(), one.size(), ResizeOptions{.filter = ResizeFilter::lanczos}));
        cmd.transform(source, one, test::sized(ctx, source.size(), one.size(), TransformOptions{.matrix = Affine::scale(1.0f / size), .filter = ResizeFilter::lanczos}));
        cmd.transform(source, destination, test::sized(ctx, source.size(), destination.size(), TransformOptions{.matrix = Affine::translate(320, 180) * Affine::rotate(33) *
                                 Affine::scale(0.004f, 0.3f),
                       .filter = ResizeFilter::bicubic,
                       .edge = EdgeMode::repeat}));
        ctx.submit_and_wait(cmd);
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        test::check(seconds < 60, "large reductions took " + std::to_string(seconds) + " s");
        const auto average = geometry::download(ctx, one);
        test::near(average[0] / average[3], 0.3f, 1e-4f);
    });
    test::run("area integrates layers smaller than their footprint exactly", [] {
        auto ctx = Context::create();
        auto one = ctx.create_image({1, 1});
        geometry::upload(ctx, one, {1, 1, 1, 1});
        // An opaque unit square scaled by 1/1000 inside one pixel covers 1e-6 of it. Kernels
        // weigh the single source pixel against the full kernel integral (F^2 for tent and
        // Catmull-Rom).
        const auto tiny =
            Affine::translate(0.5f, 0.5f) * Affine::scale(0.001f) * Affine::translate(-0.5f, -0.5f);
        for (auto filter : {ResizeFilter::area, ResizeFilter::bilinear, ResizeFilter::bicubic}) {
            const auto actual = transform(ctx, one, {1, 1}, {.matrix = tiny, .filter = filter});
            test::near(actual[3] * 1e6f, 1, 1e-3f);
        }
        // A 3x2 opaque layer at 1/100 scale placed anywhere: each alpha is the exact
        // overlap of the tiny rectangle with that pixel.
        auto layer = ctx.create_image({3, 2});
        auto cmd = ctx.create_commands(1);
        cmd.fill(layer, {.color = {1, 1, 1, 1}});
        ctx.submit_and_wait(cmd);
        for (const auto& origin : {Point{1.99f, 2.995f}, Point{0.3f, 0.7f}}) {
            const auto actual =
                transform(ctx, layer, {4, 4},
                          {.matrix = Affine::translate(origin.x, origin.y) * Affine::scale(0.01f),
                           .filter = ResizeFilter::area});
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    const double overlap_x =
                        std::max(0.0, std::min(x + 1.0, origin.x + 0.03) -
                                          std::max(double(x), double(origin.x)));
                    const double overlap_y =
                        std::max(0.0, std::min(y + 1.0, origin.y + 0.02) -
                                          std::max(double(y), double(origin.y)));
                    test::near(actual[(y * 4 + x) * 4 + 3], float(overlap_x * overlap_y), 1e-7f);
                }
            }
        }
    });

    test::run("large area transforms keep each source pixel in its own output pixel", [] {
        auto ctx = Context::create();
        // Review case: 100x1 black, only x = 65 white, scaled by 1/65 into 2x1. Output
        // pixel 0 covers source [0, 65) and pixel 1 covers [65, 130).
        Pixels line;
        for (int x = 0; x < 100; ++x) {
            const float v = x == 65 ? 1.0f : 0.0f;
            line.insert(line.end(), {v, v, v, 1});
        }
        auto source = ctx.create_image({100, 1});
        geometry::upload(ctx, source, line);
        for (auto edge :
             {EdgeMode::transparent, EdgeMode::clamp, EdgeMode::repeat, EdgeMode::mirror}) {
            const auto actual = transform(ctx, source, {2, 1},
                                          {.matrix = Affine::scale(1.0f / 65, 1),
                                           .filter = ResizeFilter::area,
                                           .edge = edge});
            // Beyond x = 100 the edge modes supply black (clamp: x = 99; repeat: 0..29;
            // mirror: 99..70) or nothing (transparent: 35 of 65 columns covered).
            const float alpha = edge == EdgeMode::transparent ? 35.0f / 65 : 1.0f;
            test::near(actual[0], 0, 1e-6f);
            test::near(actual[3], 1, 1e-6f);
            test::near(actual[4], 1.0f / 65, 1e-6f);
            test::near(actual[7], alpha, 1e-6f);
        }
    });

    test::run("large rotated area transforms match exact clipping and supersampling", [] {
        auto ctx = Context::create();
        const int sw = 300, sh = 200, dw = 16, dh = 12;
        const auto pixels = detailed(sw, sh);
        auto source = ctx.create_image({sw, sh});
        geometry::upload(ctx, source, pixels);
        // About 77x reduction along the major axis, with rotation and shear.
        const Affine matrix = Affine::translate(3.3f, 2.7f) * Affine::rotate(17) *
                              Affine{0.013f, 0.002f, -0.004f, 0.021f, 0, 0};
        const auto g = geometry::inverse_affine(matrix);
        for (auto edge :
             {EdgeMode::transparent, EdgeMode::clamp, EdgeMode::repeat, EdgeMode::mirror}) {
            const auto actual =
                transform(ctx, source, {dw, dh},
                          {.matrix = matrix, .filter = ResizeFilter::area, .edge = edge});
            // Polygon clipping of every source pixel (a different method from the
            // shader's row integrals).
            const auto exact =
                geometry::transformed(pixels, sw, sh, dw, dh, g, ResizeFilter::area, edge);
            geometry::expect_near(actual, exact, 1e-4f, "area integral");
            // Masks integrate their coverage the same way.
            auto mask = ctx.create_mask({sw, sh});
            auto reduced = ctx.create_mask({dw, dh});
            std::vector<std::uint8_t> bytes(std::size_t(sw) * sh);
            Pixels coverage;
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                bytes[i] = std::uint8_t(pixels[i * 4] * 255);
                const float c = bytes[i] / 255.0f;
                coverage.insert(coverage.end(), {c, c, c, c});
            }
            geometry::upload(ctx, mask, bytes);
            auto cmd = ctx.create_commands(8);
            cmd.transform(mask, reduced, test::sized(ctx, mask.size(), reduced.size(), TransformOptions{.matrix = matrix, .filter = ResizeFilter::area, .edge = edge}));
            ctx.submit_and_wait(cmd);
            const auto masked = geometry::download(ctx, reduced);
            const auto expected =
                geometry::transformed(coverage, sw, sh, dw, dh, g, ResizeFilter::area, edge);
            for (int i = 0; i < dw * dh; ++i) {
                test::check(std::abs(masked[i] - expected[i * 4 + 3] * 255) <= 0.51,
                            "mask area integral");
            }
            ctx.destroy(mask);
            ctx.destroy(reduced);
            // Supersampling of the destination pixels as a second, rougher reference.
            const auto fetch = geometry::fetch_of(pixels, sw, sh, edge);
            for (int y = 0; y < dh; y += 5) {
                for (int x = 0; x < dw; x += 5) {
                    double sum = 0;
                    constexpr int n = 128;
                    for (int j = 0; j < n; ++j) {
                        for (int i = 0; i < n; ++i) {
                            const double px = x + (i + 0.5) / n, py = y + (j + 0.5) / n;
                            const double qx = g[0] * px + g[1] * py + g[2];
                            const double qy = g[3] * px + g[4] * py + g[5];
                            sum += fetch((long long)std::floor(qx), (long long)std::floor(qy), 0);
                        }
                    }
                    test::near(actual[(y * dw + x) * 4], float(sum / (n * n)), 1e-2f);
                }
            }
        }
    });
    test::run("tiled area reductions on large canvases stay bounded and exact on average", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({512, 512});
        auto canvas = ctx.create_image({1536, 1024});
        // A 2x2-pixel checker averages to exactly 1/2 over any whole number of tiles.
        Pixels board;
        for (int y = 0; y < 512; ++y) {
            for (int x = 0; x < 512; ++x) {
                const float v = float((x / 2 + y / 2) % 2);
                board.insert(board.end(), {v, v, v, 1});
            }
        }
        geometry::upload(ctx, source, board);
        const auto start = std::chrono::steady_clock::now();
        // On this canvas both scales exceed the exact row-integral budget (pixels x footprint
        // > 2^22) and first average the source.
        for (float scale : {1.0f / 70, 1.0f / 1000}) {
            for (auto edge : {EdgeMode::repeat, EdgeMode::mirror}) {
                auto cmd = ctx.create_commands(8);
                cmd.transform(source, canvas, test::sized(ctx, source.size(), canvas.size(), TransformOptions{.matrix = Affine::rotate(30) * Affine::scale(scale),
                               .filter = ResizeFilter::area,
                               .edge = edge}));
                ctx.submit_and_wait(cmd);
                const auto actual = geometry::download(ctx, canvas);
                double sum = 0;
                for (std::size_t i = 0; i < actual.size(); i += 4) {
                    test::near(actual[i + 3], 1, 1e-4f);
                    sum += actual[i];
                }
                test::near(float(sum / (actual.size() / 4)), 0.5f, 2e-3f);
            }
        }
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        test::check(seconds < 60, "tiled reductions took " + std::to_string(seconds) + " s");
    });
    test::run("tiled area exactness depends on the computed region, not the canvas", [] {
        auto ctx = Context::create();
        // 100x1 black with x = 65 white, repeated (or mirrored) and scaled by 1/65 in x:
        // destination pixel x covers source columns [65 x, 65 x + 65) of the extended row.
        Pixels line;
        for (int x = 0; x < 100; ++x) {
            const float v = x == 65 ? 1.0f : 0.0f;
            line.insert(line.end(), {v, v, v, 1});
        }
        auto source = ctx.create_image({100, 1});
        geometry::upload(ctx, source, line);
        const auto expected = [](int x, EdgeMode edge) {
            int hits = 0;
            for (long long i = 65LL * x; i < 65LL * x + 65; ++i) {
                hits += geometry::edge_index(i, 100, edge) == 65;
            }
            return hits / 65.0f;
        };
        for (auto edge : {EdgeMode::repeat, EdgeMode::mirror}) {
            // Review case: a 1024x64 canvas, only {0, 0, 2, 1} computed.
            auto canvas = ctx.create_image({1024, 64});
            auto cmd = ctx.create_commands(32);
            cmd.fill(canvas, {.color = {0, 0, 1, 1}});
            cmd.transform(source, canvas, test::sized(ctx, source.size(), canvas.size(), TransformOptions{.matrix = Affine::scale(1.0f / 65, 1),
                           .filter = ResizeFilter::area,
                           .edge = edge,
                           .region = Rect{0, 0, 2, 1}}));
            ctx.submit_and_wait(cmd);
            auto actual = geometry::download(ctx, canvas);
            test::near(actual[0], expected(0, edge), 1e-6f);
            test::near(actual[4], expected(1, edge), 1e-6f);
            test::near(actual[4 * 2 + 2], 1, 0); // Outside the region: untouched.
            // The whole canvas, computed region by region (one submission each), is exact
            // everywhere; the regions are above one workgroup.
            for (int top = 0; top < 64; top += 16) {
                cmd.transform(source, canvas, test::sized(ctx, source.size(), canvas.size(), TransformOptions{.matrix = Affine::scale(1.0f / 65, 1),
                               .filter = ResizeFilter::area,
                               .edge = edge,
                               .region = Rect{0, top, 1024, 16}}));
                ctx.submit_and_wait(cmd);
            }
            actual = geometry::download(ctx, canvas);
            for (int y = 0; y < 64; ++y) {
                for (int x = 0; x < 1024; ++x) {
                    const auto i = std::size_t(y * 1024 + x) * 4;
                    test::near(actual[i], expected(x, edge), 1e-5f);
                    test::near(actual[i + 3], 1, 1e-5f);
                }
            }
            ctx.destroy(canvas);
        }
        // Masks select their region the same way.
        auto mask = ctx.create_mask({100, 1});
        std::vector<std::uint8_t> bytes(100, 0);
        bytes[65] = 255;
        geometry::upload(ctx, mask, bytes);
        auto wide = ctx.create_mask({1024, 64});
        auto cmd = ctx.create_commands(32);
        cmd.fill(wide, {.coverage = 0});
        cmd.transform(mask, wide, test::sized(ctx, mask.size(), wide.size(), TransformOptions{.matrix = Affine::scale(1.0f / 65, 1),
                       .filter = ResizeFilter::area,
                       .edge = EdgeMode::repeat,
                       .region = Rect{0, 0, 2, 1}}));
        ctx.submit_and_wait(cmd);
        const auto coverage = geometry::download(ctx, wide);
        test::check(coverage[0] == 0 && coverage[1] == 4 && coverage[2] == 0,
                    "mask region integral"); // 255 / 65 rounds to 4.
    });
    return test::finish();
}
