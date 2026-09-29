#pragma once
// CPU references for the painting tools, in double precision.
#include "../test.h"
#include <array>
#include <cstring>
#include <functional>
#include <numbers>
#include <optional>

namespace reference {
using namespace wgpupixel;
using Pixel = std::array<double, 4>;

struct Canvas {
    int width{}, height{};
    std::vector<Pixel> pixels;
    Pixel& at(int x, int y) {
        return pixels[std::size_t(y) * width + x];
    }
    const Pixel& at(int x, int y) const {
        return pixels[std::size_t(y) * width + x];
    }
};

inline Canvas pattern(int width, int height, std::uint32_t seed, bool transparent_holes = true) {
    Canvas canvas{width, height, std::vector<Pixel>(std::size_t(width) * height)};
    std::uint32_t state = seed * 747796405u + 2891336453u;
    const auto next = [&] {
        state = state * 1664525u + 1013904223u;
        return double(state >> 8) / 16777216.0;
    };
    for (auto& pixel : canvas.pixels) {
        const double alpha = transparent_holes && next() < 0.1 ? 0 : 0.2 + 0.8 * next();
        pixel = {next() * alpha, next() * alpha, next() * alpha, alpha};
    }
    return canvas;
}

inline void upload(Context& ctx, const Image& image, const Canvas& canvas) {
    std::vector<float> values;
    for (const auto& pixel : canvas.pixels) {
        for (double channel : pixel) {
            values.push_back(float(channel));
        }
    }
    auto buffer = ctx.create_upload_buffer(image, {.format = TransferFormat::rgba32_float});
    ctx.write(buffer, std::span(reinterpret_cast<const std::uint8_t*>(values.data()),
                                values.size() * sizeof(float)));
    ctx.run_and_wait([&](Commands& cmd) { cmd.upload(buffer, image); });
    ctx.destroy(buffer);
}

inline Canvas download(Context& ctx, const Image& image) {
    const auto size = image.size();
    std::vector<float> values(std::size_t(size.width) * size.height * 4);
    auto buffer = ctx.create_readback_buffer(image, {.format = TransferFormat::rgba32_float});
    ctx.run_and_wait([&](Commands& cmd) { cmd.download(image, buffer); });
    ctx.read(buffer, std::span(reinterpret_cast<std::uint8_t*>(values.data()),
                               values.size() * sizeof(float)));
    ctx.destroy(buffer);
    Canvas canvas{int(size.width), int(size.height), {}};
    for (std::size_t i = 0; i < values.size(); i += 4) {
        canvas.pixels.push_back({values[i], values[i + 1], values[i + 2], values[i + 3]});
    }
    return canvas;
}

inline void upload(Context& ctx, const Mask& mask, std::span<const std::uint8_t> bytes) {
    auto buffer = ctx.create_upload_buffer(mask);
    ctx.write(buffer, bytes);
    ctx.run_and_wait([&](Commands& cmd) { cmd.upload(buffer, mask); });
    ctx.destroy(buffer);
}

inline void expect(const Canvas& actual, const Canvas& expected, double tolerance,
                   std::source_location where = std::source_location::current()) {
    test::check(actual.pixels.size() == expected.pixels.size(), "canvas size differs", where);
    for (std::size_t i = 0; i < actual.pixels.size(); ++i) {
        for (std::size_t c = 0; c < 4; ++c) {
            const double a = actual.pixels[i][c], e = expected.pixels[i][c];
            if (!(std::abs(a - e) <= tolerance)) {
                test::check(false,
                            "pixel " + std::to_string(i % actual.width) + "," +
                                std::to_string(i / actual.width) + " channel " + std::to_string(c) +
                                ": expected " + std::to_string(e) + ", got " + std::to_string(a),
                            where);
            }
        }
    }
}

// Selection: coverage mask and region, applied as mix(before, after, coverage).
struct Selection {
    std::span<const std::uint8_t> mask{};
    std::optional<Rect> region{};
    double at(int x, int y, int width) const {
        if (region && !(x >= region->x && y >= region->y && x < region->x + region->width &&
                        y < region->y + region->height)) {
            return 0;
        }
        return mask.empty() ? 1 : mask[std::size_t(y) * width + x] / 255.0;
    }
};

inline Pixel mix(const Pixel& a, const Pixel& b, double t) {
    Pixel result;
    for (std::size_t c = 0; c < 4; ++c) {
        result[c] = a[c] + (b[c] - a[c]) * t;
    }
    return result;
}

inline double blend_channel(double b, double s, BlendMode mode) {
    switch (mode) {
    case BlendMode::multiply:
        return b * s;
    case BlendMode::screen:
        return b + s - b * s;
    case BlendMode::darken:
        return std::min(b, s);
    case BlendMode::lighten:
        return std::max(b, s);
    default:
        return s;
    }
}

// Premultiplied source-over with a separable blend function.
inline Pixel blend(const Pixel& source, const Pixel& backdrop, double opacity, BlendMode mode) {
    Pixel s;
    for (std::size_t c = 0; c < 4; ++c) {
        s[c] = source[c] * opacity;
    }
    if (s[3] == 0) {
        return backdrop;
    }
    Pixel result;
    result[3] = s[3] + backdrop[3] * (1 - s[3]);
    for (std::size_t c = 0; c < 3; ++c) {
        if (mode == BlendMode::normal || backdrop[3] == 0) {
            result[c] = s[c] + backdrop[c] * (1 - s[3]);
        } else {
            const double mixed =
                blend_channel(backdrop[c] / backdrop[3], source[c] / source[3], mode);
            result[c] =
                (1 - s[3]) * backdrop[c] + (1 - backdrop[3]) * s[c] + s[3] * backdrop[3] * mixed;
        }
    }
    return result;
}

// sRGB transfer mirrored for negatives.
inline double srgb_encode(double v) {
    const double a = std::abs(v);
    return std::copysign(a <= 0.0031308 ? a * 12.92 : 1.055 * std::pow(a, 1 / 2.4) - 0.055, v);
}
inline double srgb_decode(double v) {
    const double a = std::abs(v);
    return std::copysign(a <= 0.04045 ? a / 12.92 : std::pow((a + 0.055) / 1.055, 2.4), v);
}

// Axes below one pixel render one pixel wide; the gain restores their area.
inline std::array<double, 4> shape(const BrushDab& dab) {
    const double rx = std::max(0.5 * dab.diameter, 0.5);
    const double ry = std::max(0.5 * dab.diameter * dab.roundness, 0.5);
    const double radians = dab.angle * std::numbers::pi / 180;
    const double s = std::sin(radians), k = std::cos(radians);
    return {k / rx, s / rx, -s / ry, k / ry};
}
inline double gain(const BrushDab& dab) {
    const double rx = 0.5 * dab.diameter, ry = rx * dab.roundness;
    return rx * ry / (std::max(rx, 0.5) * std::max(ry, 0.5));
}

inline double round_profile(double dx, double dy, const std::array<double, 4>& m, double hardness,
                            double ramp) {
    const double ux = m[0] * dx + m[1] * dy, uy = m[2] * dx + m[3] * dy;
    const double radius = std::hypot(ux, uy);
    if (radius <= 1e-6) {
        return 1;
    }
    const double gradient = std::hypot(m[0] * ux + m[2] * uy, m[1] * ux + m[3] * uy) / radius;
    const double edge = (1 - radius) / gradient;
    const double softness = std::max(ramp, (1 - hardness) / gradient);
    const double t = std::clamp((edge + 0.5 * ramp) / softness, 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

inline double round_coverage(double dx, double dy, const std::array<double, 4>& m,
                             double hardness) {
    const double major = 1 / std::hypot(m[0], m[1]), minor = 1 / std::hypot(m[2], m[3]);
    if (minor >= 2 && minor * minor >= major) {
        return round_profile(dx, dy, m, hardness, 1);
    }
    double sum = 0;
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            sum += round_profile(dx + (i + 0.5) / 4 - 0.5, dy + (j + 0.5) / 4 - 0.5, m, hardness,
                                 0.25);
        }
    }
    return sum / 16;
}

struct Tip {
    int width{}, height{};
    std::vector<std::uint8_t> coverage;
    double value(int x, int y) const {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return 0;
        }
        return coverage[std::size_t(y) * width + x] / 255.0;
    }
    double bilinear(double x, double y) const {
        const double qx = x - 0.5, qy = y - 0.5;
        const double ox = std::floor(qx), oy = std::floor(qy);
        const double fx = qx - ox, fy = qy - oy;
        const int px = int(ox), py = int(oy);
        const double top = value(px, py) * (1 - fx) + value(px + 1, py) * fx;
        const double bottom = value(px, py + 1) * (1 - fx) + value(px + 1, py + 1) * fx;
        return top * (1 - fy) + bottom * fy;
    }
};

inline double tip_coverage(double dx, double dy, const std::array<double, 4>& m, const Tip& tip) {
    const double scale = std::max(tip.width, tip.height) / 2.0;
    const double texels = scale * std::max(std::hypot(m[0], m[2]), std::hypot(m[1], m[3]));
    const int taps = int(std::clamp(std::ceil(texels), 1.0, 4.0));
    double sum = 0;
    for (int j = 0; j < taps; ++j) {
        for (int i = 0; i < taps; ++i) {
            const double x = dx + (i + 0.5) / taps - 0.5, y = dy + (j + 0.5) / taps - 0.5;
            sum += tip.bilinear(scale * (m[0] * x + m[1] * y) + tip.width / 2.0,
                                scale * (m[2] * x + m[3] * y) + tip.height / 2.0);
        }
    }
    return sum / (taps * taps);
}

inline double dab_coverage(const BrushDab& dab, int x, int y, double hardness, const Tip* tip) {
    const double dx = x + 0.5 - dab.center.x, dy = y + 0.5 - dab.center.y;
    const auto m = shape(dab);
    return gain(dab) * (tip ? tip_coverage(dx, dy, m, *tip) : round_coverage(dx, dy, m, hardness));
}

inline std::vector<BrushDab> dabs(std::span<const StrokeSample> samples, const Brush& brush) {
    std::vector<BrushDab> result(brush_dabs(samples, brush));
    test::check(brush_dabs(samples, brush, result) == result.size(), "dab count changed");
    return result;
}

// Photoshop-style stroke coverage per pixel: opacity caps, flow builds up.
inline std::vector<double> stroke(int width, int height, std::span<const StrokeSample> samples,
                                  const Brush& brush, double opacity, double flow,
                                  const Tip* tip = nullptr) {
    const auto placed = dabs(samples, brush);
    std::vector<double> coverage(std::size_t(width) * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double s = 0;
            for (const auto& dab : placed) {
                const double cap = opacity * dab.opacity;
                if (s >= cap) {
                    continue;
                }
                s += (cap - s) * flow * dab.flow * dab_coverage(dab, x, y, brush.hardness, tip);
            }
            coverage[std::size_t(y) * width + x] = s;
        }
    }
    return coverage;
}

// Applies tool(pixel, x, y, stroke) where the stroke is positive, then the selection.
inline Canvas apply(const Canvas& before, const std::vector<double>& coverage,
                    const Selection& selection,
                    const std::function<Pixel(const Pixel&, int, int, double)>& tool) {
    auto result = before;
    for (int y = 0; y < before.height; ++y) {
        for (int x = 0; x < before.width; ++x) {
            const double s = coverage[std::size_t(y) * before.width + x];
            const double selected = selection.at(x, y, before.width);
            if (s <= 0 || selected <= 0) {
                continue;
            }
            result.at(x, y) = mix(before.at(x, y), tool(before.at(x, y), x, y, s), selected);
        }
    }
    return result;
}

inline std::vector<std::uint8_t> ramp_mask(int width, int height) {
    std::vector<std::uint8_t> bytes(std::size_t(width) * height);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = std::array<std::uint8_t, 5>{255, 0, 128, 255, 64}[i % 5];
    }
    return bytes;
}
} // namespace reference
