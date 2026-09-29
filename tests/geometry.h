#pragma once

#include "test.h"
#include <array>
#include <cstring>
#include <functional>
#include <numbers>

// Double-precision CPU references for the geometry operations, written from the
// documented semantics, plus lossless float transfers for exact comparisons.
namespace geometry {
using namespace wgpupixel;
using Pixels = std::vector<float>;

inline void upload(Context& ctx, const Image& image, const Pixels& pixels) {
    auto buffer = ctx.create_upload_buffer(image, {.format = TransferFormat::rgba32_float});
    ctx.write(buffer, {reinterpret_cast<const std::uint8_t*>(pixels.data()), pixels.size() * 4});
    auto cmd = ctx.create_commands(1);
    cmd.upload(buffer, image);
    ctx.submit_and_wait(cmd);
    ctx.destroy(buffer);
}

inline Pixels download(Context& ctx, const Image& image) {
    Pixels pixels(std::size_t(image.size().width) * image.size().height * 4);
    auto buffer = ctx.create_readback_buffer(image, {.format = TransferFormat::rgba32_float});
    auto cmd = ctx.create_commands(1);
    cmd.download(image, buffer);
    ctx.submit_and_wait(cmd);
    ctx.read(buffer, {reinterpret_cast<std::uint8_t*>(pixels.data()), pixels.size() * 4});
    ctx.destroy(buffer);
    return pixels;
}

inline void upload(Context& ctx, const Mask& mask, const std::vector<std::uint8_t>& coverage) {
    auto buffer = ctx.create_upload_buffer(mask);
    ctx.write(buffer, coverage);
    auto cmd = ctx.create_commands(1);
    cmd.upload(buffer, mask);
    ctx.submit_and_wait(cmd);
    ctx.destroy(buffer);
}

inline std::vector<std::uint8_t> download(Context& ctx, const Mask& mask) {
    std::vector<std::uint8_t> coverage(std::size_t(mask.size().width) * mask.size().height);
    auto buffer = ctx.create_readback_buffer(mask);
    auto cmd = ctx.create_commands(1);
    cmd.download(mask, buffer);
    ctx.submit_and_wait(cmd);
    ctx.read(buffer, coverage);
    ctx.destroy(buffer);
    return coverage;
}

// Deterministic semitransparent premultiplied test pattern with HDR values.
inline Pixels pattern(int width, int height, int seed = 0) {
    Pixels pixels;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int i = x * 7 + y * 13 + seed;
            const float a = 0.25f + 0.25f * float(i % 4);
            pixels.insert(pixels.end(), {a * float((i * 3) % 11) / 8.0f, a * float(y % 5) / 4.0f,
                                         a * float(x % 3) / 2.0f, a});
        }
    }
    return pixels;
}

inline double kernel(double t, ResizeFilter filter) {
    const double x = std::abs(t);
    switch (filter) {
    case ResizeFilter::bicubic:
        return x <= 1  ? 1 - 2.5 * x * x + 1.5 * x * x * x
               : x < 2 ? 2 - 4 * x + 2.5 * x * x - 0.5 * x * x * x
                       : 0;
    case ResizeFilter::lanczos: {
        if (x >= 3) {
            return 0;
        }
        if (x < 1e-9) {
            return 1;
        }
        const double p = std::numbers::pi * x;
        return 3 * std::sin(p) * std::sin(p / 3) / (p * p);
    }
    default:
        return std::max(1 - x, 0.0);
    }
}

inline int edge_index(long long i, int extent, EdgeMode edge) {
    switch (edge) {
    case EdgeMode::clamp:
        return int(std::clamp<long long>(i, 0, extent - 1));
    case EdgeMode::repeat:
        return int(((i % extent) + extent) % extent);
    case EdgeMode::mirror: {
        const long long m = ((i % (2 * extent)) + 2 * extent) % (2 * extent);
        return int(m < extent ? m : 2 * extent - 1 - m);
    }
    default:
        return i < 0 || i >= extent ? -1 : int(i);
    }
}

// The source seen through an edge mode: fetch(x, y, channel).
using Fetch = std::function<double(long long, long long, int)>;
inline Fetch fetch_of(const Pixels& pixels, int width, int height, EdgeMode edge,
                      int channels = 4) {
    return [&pixels, width, height, edge, channels](long long x, long long y, int c) -> double {
        const int ix = edge_index(x, width, edge), iy = edge_index(y, height, edge);
        if (ix < 0 || iy < 0) {
            return 0;
        }
        return pixels[(std::size_t(iy) * width + ix) * channels + c];
    };
}

// Exact area of the convex polygon `polygon` inside the box [x0, x1] x [y0, y1].
inline double clipped_area(std::vector<std::array<double, 2>> polygon, double x0, double y0,
                           double x1, double y1) {
    const auto clip = [&](auto inside, auto cross) {
        std::vector<std::array<double, 2>> out;
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            const auto a = polygon[i], b = polygon[(i + 1) % polygon.size()];
            if (inside(a)) {
                out.push_back(a);
            }
            if (inside(a) != inside(b)) {
                out.push_back(cross(a, b));
            }
        }
        polygon = out;
    };
    const auto at_x = [](double x) {
        return [x](auto a, auto b) {
            const double t = (x - a[0]) / (b[0] - a[0]);
            return std::array{x, a[1] + t * (b[1] - a[1])};
        };
    };
    const auto at_y = [](double y) {
        return [y](auto a, auto b) {
            const double t = (y - a[1]) / (b[1] - a[1]);
            return std::array{a[0] + t * (b[0] - a[0]), y};
        };
    };
    clip([&](auto v) { return v[0] >= x0; }, at_x(x0));
    clip([&](auto v) { return v[0] <= x1; }, at_x(x1));
    clip([&](auto v) { return v[1] >= y0; }, at_y(y0));
    clip([&](auto v) { return v[1] <= y1; }, at_y(y1));
    double twice = 0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const auto a = polygon[i], b = polygon[(i + 1) % polygon.size()];
        twice += a[0] * b[1] - a[1] * b[0];
    }
    return 0.5 * std::abs(twice);
}

// Footprint sampling as documented at ResizeFilter: the Jacobian J columns map
// destination steps into the source. area averages the source over the exact
// back-projected pixel (a parallelogram). The kernels are evaluated on S d where
// S = F^-1 and F = f(J J^T) clamps the principal lengths to [1, limit_radius / radius]
// (32 with transparent edges, 12 otherwise).
inline std::array<double, 4> sample(const Fetch& fetch, double qx, double qy,
                                    std::array<double, 4> j, ResizeFilter filter, int channels = 4,
                                    double limit_radius = 32) {
    std::array<double, 4> result{};
    if (filter == ResizeFilter::nearest) {
        for (int c = 0; c < channels; ++c) {
            result[c] = fetch((long long)std::floor(qx), (long long)std::floor(qy), c);
        }
        return result;
    }
    std::array<double, 4> sum{};
    double total = 0;
    const auto add = [&](long long x, long long y, double w) {
        if (w == 0) {
            return;
        }
        total += w;
        for (int c = 0; c < channels; ++c) {
            sum[c] += w * fetch(x, y, c);
        }
    };
    if (filter == ResizeFilter::area) {
        // Parallelogram corners q +- J (1/2, 1/2); weights are exact overlap areas.
        std::vector<std::array<double, 2>> corners;
        for (const auto [u, v] : {std::pair{-0.5, -0.5}, {0.5, -0.5}, {0.5, 0.5}, {-0.5, 0.5}}) {
            corners.push_back({qx + j[0] * u + j[2] * v, qy + j[1] * u + j[3] * v});
        }
        double left = INFINITY, top = INFINITY, right = -INFINITY, bottom = -INFINITY;
        for (const auto& c : corners) {
            left = std::min(left, c[0]);
            right = std::max(right, c[0]);
            top = std::min(top, c[1]);
            bottom = std::max(bottom, c[1]);
        }
        for (long long y = (long long)std::floor(top); y < bottom; ++y) {
            for (long long x = (long long)std::floor(left); x < right; ++x) {
                add(x, y, clipped_area(corners, double(x), double(y), x + 1.0, y + 1.0));
            }
        }
    } else {
        // Eigen decomposition of C = J J^T with J = [j0 j2; j1 j3].
        const double p = j[0] * j[0] + j[2] * j[2], r = j[1] * j[1] + j[3] * j[3];
        const double k = j[0] * j[1] + j[2] * j[3];
        const double spread = std::sqrt(0.25 * (p - r) * (p - r) + k * k);
        const double l1 = 0.5 * (p + r) + spread, l2 = std::max(0.5 * (p + r) - spread, 0.0);
        const double radius = filter == ResizeFilter::bilinear  ? 1
                              : filter == ResizeFilter::bicubic ? 2
                                                                : 3;
        const auto f = [&](double l) {
            return std::clamp(std::sqrt(l), 1.0, limit_radius / radius);
        };
        double ux = 1, uy = 0;
        if (spread > 1e-12 * (p + r)) {
            ux = k;
            uy = l1 - p;
            if (std::abs(ux) + std::abs(uy) < 1e-300) {
                ux = l1 - r;
                uy = k;
            }
            const double n = std::hypot(ux, uy);
            ux /= n;
            uy /= n;
        }
        const double f1 = f(l1), f2 = f(l2);
        const double F00 = f1 * ux * ux + f2 * uy * uy, F01 = (f1 - f2) * ux * uy,
                     F11 = f1 * uy * uy + f2 * ux * ux;
        const double det = F00 * F11 - F01 * F01;
        const double S00 = F11 / det, S01 = -F01 / det, S11 = F00 / det;
        const double hx = radius * (std::abs(F00) + std::abs(F01)),
                     hy = radius * (std::abs(F01) + std::abs(F11));
        for (long long y = (long long)std::ceil(qy - hy - 0.5); y <= qy + hy - 0.5; ++y) {
            for (long long x = (long long)std::ceil(qx - hx - 0.5); x <= qx + hx - 0.5; ++x) {
                const double dx = x + 0.5 - qx, dy = y + 0.5 - qy;
                add(x, y,
                    kernel(S00 * dx + S01 * dy, filter) * kernel(S01 * dx + S11 * dy, filter));
            }
        }
    }
    if (std::abs(total) < 1e-300) {
        return result;
    }
    for (int c = 0; c < channels; ++c) {
        result[c] = sum[c] / total;
    }
    return result;
}

// Destination-to-source projective map in double: q = (G p) / w, empty where w <= 0.
using Matrix = std::array<double, 9>;

inline Matrix inverse_affine(const Affine& m) {
    const auto inverted = inverse(Homography::from(m));
    return inverted->m;
}

// Largest principal length of the footprint of destination pixel center (px, py);
// infinite beyond the horizon. Operations sample footprints wider than
// 32 / kernel radius on a coarser pyramid level, which these references do not model.
inline double footprint_length(const Matrix& g, double px, double py) {
    const double w = g[6] * px + g[7] * py + g[8];
    if (!(w > 0)) {
        return INFINITY;
    }
    const double qx = (g[0] * px + g[1] * py + g[2]) / w, qy = (g[3] * px + g[4] * py + g[5]) / w;
    const double a = (g[0] - qx * g[6]) / w, b = (g[3] - qy * g[6]) / w;
    const double c = (g[1] - qx * g[7]) / w, d = (g[4] - qy * g[7]) / w;
    const double p = a * a + c * c, r = b * b + d * d, k = a * b + c * d;
    return std::sqrt(0.5 * (p + r) + std::sqrt(0.25 * (p - r) * (p - r) + k * k));
}

inline double direct_limit(ResizeFilter filter, EdgeMode edge = EdgeMode::transparent) {
    constexpr std::array radius{0.5, 1.0, 2.0, 3.0, 0.5};
    if (edge != EdgeMode::transparent && filter == ResizeFilter::area) {
        return 6;
    }
    return (edge == EdgeMode::transparent ? 32 : 12) / radius[std::to_underlying(filter)];
}

// Transforms a whole image through G (limit_radius: see sample; 0 picks the transform
// limits, resize has none) (destination to source), as transform/perspective.
inline Pixels transformed(const Pixels& pixels, int sw, int sh, int dw, int dh, const Matrix& g,
                          ResizeFilter filter, EdgeMode edge, int channels = 4,
                          double limit_radius = 0) {
    Pixels result(std::size_t(dw) * dh * channels);
    const auto fetch = fetch_of(pixels, sw, sh, edge, channels);
    for (int y = 0; y < dh; ++y) {
        for (int x = 0; x < dw; ++x) {
            const double px = x + 0.5, py = y + 0.5;
            const double w = g[6] * px + g[7] * py + g[8];
            if (!(w > 0)) {
                continue;
            }
            const double u = g[0] * px + g[1] * py + g[2];
            const double v = g[3] * px + g[4] * py + g[5];
            const double qx = u / w, qy = v / w;
            const std::array<double, 4> jacobian{(g[0] - qx * g[6]) / w, (g[3] - qy * g[6]) / w,
                                                 (g[1] - qx * g[7]) / w, (g[4] - qy * g[7]) / w};
            if (edge == EdgeMode::transparent && filter == ResizeFilter::nearest &&
                (qx < 0 || qy < 0 || qx >= sw || qy >= sh)) {
                continue;
            }
            const auto value = sample(fetch, qx, qy, jacobian, filter, channels,
                                      limit_radius > 0                ? limit_radius
                                      : edge == EdgeMode::transparent ? 32
                                                                      : 12);
            for (int c = 0; c < channels; ++c) {
                result[(std::size_t(y) * dw + x) * channels + c] = float(value[c]);
            }
        }
    }
    return result;
}

inline void expect_near(const Pixels& actual, const Pixels& expected, float tolerance,
                        std::string_view what) {
    test::check(actual.size() == expected.size(), std::string(what) + ": size differs");
    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (!(std::abs(actual[i] - expected[i]) <= tolerance)) {
            test::check(false, std::string(what) + ": component " + std::to_string(i) + " got " +
                                   std::to_string(actual[i]) + ", expected " +
                                   std::to_string(expected[i]));
        }
    }
}
} // namespace geometry
