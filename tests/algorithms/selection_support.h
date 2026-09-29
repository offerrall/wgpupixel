#pragma once
// Mask transfers and CPU references shared by the selection algorithm tests.
#include "../test.h"
#include <array>
#include <cstdint>
#include <random>
#include <vector>

namespace selection {
using Bytes = std::vector<std::uint8_t>;

inline void upload(wgpupixel::Context& ctx, const wgpupixel::Mask& mask,
                   std::span<const std::uint8_t> bytes) {
    auto buffer = ctx.create_upload_buffer(mask);
    ctx.write(buffer, bytes);
    ctx.run_and_wait([&](wgpupixel::Commands& cmd) { cmd.upload(buffer, mask); });
    ctx.destroy(buffer);
}

inline Bytes read(wgpupixel::Context& ctx, const wgpupixel::Mask& mask) {
    auto buffer = ctx.create_readback_buffer(mask);
    ctx.run_and_wait([&](wgpupixel::Commands& cmd) { cmd.download(mask, buffer); });
    Bytes bytes(std::size_t(mask.size().width) * mask.size().height);
    ctx.read(buffer, bytes);
    ctx.destroy(buffer);
    return bytes;
}

// Uploads straight sRGB RGBA8 pixels.
inline void upload(wgpupixel::Context& ctx, const wgpupixel::Image& image,
                   std::span<const std::uint8_t> rgba) {
    auto buffer = ctx.create_upload_buffer(image);
    ctx.write(buffer, rgba);
    ctx.run_and_wait([&](wgpupixel::Commands& cmd) { cmd.upload(buffer, image); });
    ctx.destroy(buffer);
}

inline Bytes random_bytes(std::size_t count, std::uint32_t seed) {
    std::mt19937 random(seed);
    Bytes bytes(count);
    for (auto& byte : bytes) {
        byte = static_cast<std::uint8_t>(random() & 255);
    }
    return bytes;
}

// Blobs of hard coverage with a few soft pixels, so edges and islands are exercised.
inline Bytes blobs(int width, int height, std::uint32_t seed, bool soft = false) {
    std::mt19937 random(seed);
    std::vector<std::array<double, 3>> discs;
    for (int i = 0; i < 5; ++i) {
        discs.push_back(
            {double(random() % width), double(random() % height), 1.0 + double(random() % 7)});
    }
    Bytes bytes(std::size_t(width) * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            bool inside = random() % 97 == 0; // Isolated specks.
            for (const auto& disc : discs) {
                inside |= std::hypot(x - disc[0], y - disc[1]) <= disc[2];
            }
            auto& byte = bytes[std::size_t(y) * width + x];
            byte = inside ? 255 : 0;
            if (soft && random() % 5 == 0) {
                byte = static_cast<std::uint8_t>(random() & 255);
            }
        }
    }
    return bytes;
}

inline std::uint8_t quantize(double coverage) {
    return static_cast<std::uint8_t>(std::floor(std::clamp(coverage, 0.0, 1.0) * 255 + 0.5));
}

inline std::uint8_t combine(std::uint8_t existing, std::uint8_t incoming,
                            wgpupixel::SelectionMode mode) {
    using wgpupixel::SelectionMode;
    switch (mode) {
    case SelectionMode::add:
        return std::max(existing, incoming);
    case SelectionMode::subtract:
        return existing > incoming ? existing - incoming : 0;
    case SelectionMode::intersect:
        return std::min(existing, incoming);
    case SelectionMode::difference:
        return static_cast<std::uint8_t>(std::abs(int(existing) - int(incoming)));
    default:
        return incoming;
    }
}

inline void expect(std::span<const std::uint8_t> actual, std::span<const std::uint8_t> expected,
                   int tolerance, std::string_view what, int width = 0) {
    test::check(actual.size() == expected.size(), std::string(what) + ": size mismatch");
    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (std::abs(int(actual[i]) - int(expected[i])) > tolerance) {
            const auto where =
                width ? " at (" + std::to_string(i % width) + ", " + std::to_string(i / width) + ")"
                      : " at " + std::to_string(i);
            test::check(false, std::string(what) + where + ": expected " +
                                   std::to_string(expected[i]) + ", got " +
                                   std::to_string(actual[i]));
        }
    }
}

// Area of a polygon clipped to the unit pixel at (x, y): exact coverage for a simple
// polygon (Sutherland-Hodgman against the convex pixel square, then the shoelace sum).
using Polygon = std::vector<std::array<double, 2>>;
inline double pixel_area(const Polygon& polygon, int x, int y) {
    Polygon current = polygon;
    const std::array<std::array<double, 3>, 4> planes{
        {{1, 0, double(x)}, {-1, 0, -double(x + 1)}, {0, 1, double(y)}, {0, -1, -double(y + 1)}}};
    for (const auto& plane : planes) {
        Polygon next;
        const auto side = [&](const std::array<double, 2>& p) {
            return plane[0] * p[0] + plane[1] * p[1] - plane[2];
        };
        for (std::size_t i = 0; i < current.size(); ++i) {
            const auto& a = current[i];
            const auto& b = current[(i + 1) % current.size()];
            const double sa = side(a), sb = side(b);
            if (sa >= 0) {
                next.push_back(a);
            }
            if ((sa >= 0) != (sb >= 0)) {
                const double t = sa / (sa - sb);
                next.push_back({a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1])});
            }
        }
        current = std::move(next);
        if (current.empty()) {
            return 0;
        }
    }
    double area = 0;
    for (std::size_t i = 0; i < current.size(); ++i) {
        const auto& a = current[i];
        const auto& b = current[(i + 1) % current.size()];
        area += a[0] * b[1] - b[0] * a[1];
    }
    return std::abs(area) / 2;
}

inline Bytes rasterize(const Polygon& polygon, int width, int height) {
    Bytes bytes(std::size_t(width) * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            bytes[std::size_t(y) * width + x] = quantize(pixel_area(polygon, x, y));
        }
    }
    return bytes;
}

// Winding number of a point with respect to closed contours.
inline int winding(const std::vector<Polygon>& contours, double px, double py) {
    int total = 0;
    for (const auto& contour : contours) {
        for (std::size_t i = 0; i < contour.size(); ++i) {
            const auto& a = contour[i];
            const auto& b = contour[(i + 1) % contour.size()];
            if ((a[1] <= py) == (b[1] <= py)) {
                continue;
            }
            const double x = a[0] + (py - a[1]) * (b[0] - a[0]) / (b[1] - a[1]);
            if (x < px) {
                total += b[1] > a[1] ? 1 : -1;
            }
        }
    }
    return total;
}
} // namespace selection
