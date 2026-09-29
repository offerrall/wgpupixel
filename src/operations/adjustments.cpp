#include "utils/operations.h"
#include <vector>
#include <cstdio>
#include <string>
#include <limits>

namespace wgpupixel {
using namespace detail;
namespace {
void range(const Operation& op, float x, float low, float high, std::string_view name) {
    if (!std::isfinite(x) || x < low || x > high) {
        char message[192];
        std::snprintf(message, sizeof(message), "%.*s must be finite and in [%g, %g]",
                      int(name.size()), name.data(), double(low), double(high));
        op.require(false, name, message);
    }
}
void domain(const Operation& op, ColorEncoding value) {
    op.require(value == ColorEncoding::srgb || value == ColorEncoding::linear, "domain",
               "invalid adjustment domain");
}
void opaque(const Operation& op, Color c, std::string_view name) {
    range(op, c.r, 0, 1, name);
    range(op, c.g, 0, 1, name);
    range(op, c.b, 0, 1, name);
    op.require(c.a == 1, name, "color must be opaque");
}
bool tables(Operation& op, Record& record, std::array<std::span<const float>, 4> curves,
            bool spline_slopes = false) {
    for (auto& curve : curves) {
        if (spline_slopes) {
            continue;
        }
        bool identity = true;
        for (std::size_t i = 0; i < curve.size(); ++i) {
            identity &= curve[i] == float(i) / float(curve.size() - 1);
        }
        if (identity) {
            curve = {};
        }
    }
    std::size_t count = 0;
    for (auto curve : curves) {
        count += curve.size();
    }
    if (count == 0) {
        return false;
    }
    auto words = op.data(record, count, "tables");
    std::size_t offset = 0;
    for (std::size_t c = 0; c < 4; ++c) {
        record.parameters.reserved[c] = static_cast<std::uint32_t>(curves[c].size());
        if (!spline_slopes && !curves[c].empty()) {
            const auto n = curves[c].size();
            record.parameters.extra[c] = float((double(curves[c][1]) - curves[c][0]) * (n - 1));
            record.parameters.extra2[c] =
                float((double(curves[c][n - 1]) - curves[c][n - 2]) * (n - 1));
            op.require(std::isfinite(record.parameters.extra[c]) &&
                           std::isfinite(record.parameters.extra2[c]),
                       "tables", "endpoint slopes must fit finite float32");
        }
        for (float v : curves[c]) {
            words[offset++] = std::bit_cast<std::uint32_t>(v);
        }
    }
    return true;
}
std::vector<float> spline(const Operation& op, std::span<const Point> points, std::string_view name,
                          float& first_slope, float& last_slope) {
    if (points.empty()) {
        return {};
    }
    op.require(points.size() >= 2 && points.size() <= 4096, name, "expected 2..4096 points");
    for (std::size_t i = 0; i < points.size(); ++i) {
        range(op, points[i].x, 0, 1, name);
        range(op, points[i].y, 0, 1, name);
        op.require(i == 0 || points[i].x > points[i - 1].x, name,
                   "point x coordinates must strictly increase");
    }
    if (points.front().x == 0 && points.back().x == 1 &&
        std::ranges::all_of(points, [](Point p) { return p.x == p.y; })) {
        return {};
    }
    // PCHIP slopes: harmonic interior means and limited one-sided endpoints.
    const auto n = points.size();
    std::vector<double> h(n - 1), d(n - 1), m(n);
    for (std::size_t i = 0; i + 1 < n; ++i) {
        h[i] = double(points[i + 1].x) - points[i].x;
        d[i] = (double(points[i + 1].y) - points[i].y) / h[i];
    }
    m[0] = d[0];
    m[n - 1] = d[n - 2];
    for (std::size_t i = 1; i + 1 < n; ++i) {
        if (d[i - 1] * d[i] > 0) {
            const double a = 2 * h[i] + h[i - 1], b = h[i] + 2 * h[i - 1];
            m[i] = (a + b) / (a / d[i - 1] + b / d[i]);
        }
    }
    if (n > 2) {
        auto endpoint = [](double h0, double h1, double d0, double d1) {
            double v = ((2 * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
            if (v * d0 <= 0) {
                return 0.0;
            }
            if (d0 * d1 <= 0 && std::abs(v) > 3 * std::abs(d0)) {
                return 3 * d0;
            }
            return v;
        };
        m[0] = endpoint(h[0], h[1], d[0], d[1]);
        m[n - 1] = endpoint(h[n - 2], h[n - 3], d[n - 2], d[n - 3]);
    }
    first_slope = float(m.front());
    last_slope = float(m.back());
    op.require(std::isfinite(first_slope) && std::isfinite(last_slope), name,
               "endpoint slopes must fit finite float32");
    std::vector<float> result(4096);
    std::size_t j = 0;
    for (std::size_t i = 0; i < result.size(); ++i) {
        const double x = double(i) / (result.size() - 1);
        if (x <= points.front().x) {
            result[i] = points.front().y;
            continue;
        }
        if (x >= points.back().x) {
            result[i] = points.back().y;
            continue;
        }
        while (x > points[j + 1].x) {
            ++j;
        }
        const double t = (x - points[j].x) / h[j], t2 = t * t, t3 = t2 * t;
        result[i] = static_cast<float>(
            std::clamp((2 * t3 - 3 * t2 + 1) * points[j].y + (t3 - 2 * t2 + t) * h[j] * m[j] +
                           (-2 * t3 + 3 * t2) * points[j + 1].y + (t3 - t2) * h[j] * m[j + 1],
                       0.0, 1.0));
    }
    return result;
}
} // namespace

void Commands::levels(const Image& destination, const LevelsOptions& options) {
    Operation op(recording_.get(), "levels");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& pixels = op.resource(destination.resource_, "destination");
    auto record = kernel_record(Kernel::levels, pixels);
    std::array<float, 20> values{};
    const std::array channels{options.composite, options.red, options.green, options.blue};
    const std::array names{"composite", "red", "green", "blue"};
    bool identity = true;
    for (std::size_t i = 0; i < 4; ++i) {
        const auto c = channels[i];
        const std::string prefix = std::string(names[i]) + ".";
        range(op, c.input_black, 0, 1, prefix + "input_black");
        range(op, c.input_white, 0, 1, prefix + "input_white");
        range(op, c.gamma, 0.01f, 9.99f, prefix + "gamma");
        range(op, c.output_black, 0, 1, prefix + "output_black");
        range(op, c.output_white, 0, 1, prefix + "output_white");
        op.require(c.input_white > c.input_black, prefix + "input_white",
                   "input_white must exceed input_black in the same channel");
        identity &= c.input_black == 0 && c.input_white == 1 && c.gamma == 1 &&
                    c.output_black == 0 && c.output_white == 1;
        values[5 * i] = c.input_black;
        values[5 * i + 1] = c.input_white;
        values[5 * i + 2] = c.gamma;
        values[5 * i + 3] = c.output_black;
        values[5 * i + 4] = c.output_white;
    }
    if (identity) {
        return;
    }
    op.data(record, std::span<const float>(values), "channels");
    op.append({record});
}

void Commands::curves(const Image& destination, const CurvesOptions& options) {
    Operation op(recording_.get(), "curves");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& pixels = op.resource(destination.resource_, "destination");
    auto record = kernel_record(Kernel::lut, pixels);
    const auto composite = spline(op, options.composite, "composite", record.parameters.extra[0],
                                  record.parameters.extra2[0]);
    const auto red =
        spline(op, options.red, "red", record.parameters.extra[1], record.parameters.extra2[1]);
    const auto green =
        spline(op, options.green, "green", record.parameters.extra[2], record.parameters.extra2[2]);
    const auto blue =
        spline(op, options.blue, "blue", record.parameters.extra[3], record.parameters.extra2[3]);
    if (!tables(op, record, {composite, red, green, blue}, true)) {
        return;
    }
    op.append({record});
}

void Commands::lut(const Image& destination, const LutOptions& options) {
    Operation op(recording_.get(), "lut");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& pixels = op.resource(destination.resource_, "destination");
    auto record = kernel_record(Kernel::lut, pixels);
    domain(op, options.domain);
    const std::array curves{options.composite, options.red, options.green, options.blue};
    const std::array names{"composite", "red", "green", "blue"};
    for (std::size_t i = 0; i < 4; ++i) {
        op.require(curves[i].empty() || (curves[i].size() >= 2 && curves[i].size() <= 65536),
                   names[i], "expected empty or 2..65536 samples");
        for (float x : curves[i]) {
            require_finite(op, x, names[i]);
        }
    }
    record.parameters.offsets[0] = options.domain == ColorEncoding::linear;
    if (!tables(op, record, curves)) {
        return;
    }
    op.append({record});
}

void Commands::lut3d(const Image& destination, const Lut3DOptions& options) {
    Operation op(recording_.get(), "lut3d");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& pixels = op.resource(destination.resource_, "destination");
    auto record = kernel_record(Kernel::lut3d, pixels);
    domain(op, options.domain);
    op.require(options.size >= 2 && options.size <= 65, "size", "size must lie in [2, 65]");
    op.require(options.values.size() == std::size_t(options.size) * options.size * options.size * 3,
               "values", "expected size cubed RGB triplets");
    for (float x : options.values) {
        require_finite(op, x, "values");
        if (options.domain == ColorEncoding::srgb) {
            const double v = std::abs(double(x));
            const double linear = v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
            op.require(linear <= std::numeric_limits<float>::max(), "values",
                       "sRGB values must decode to finite linear float32");
        }
    }
    for (std::size_t i = 0; i < 3; ++i) {
        require_finite(op, options.domain_min[i], "domain_min");
        require_finite(op, options.domain_max[i], "domain_max");
        const float width = options.domain_max[i] - options.domain_min[i];
        op.require(width > 0 && std::isnormal(width), "domain_max",
                   "each domain_max - domain_min must be positive, finite and normal in float32");
        record.parameters.color1[i] = options.domain_min[i];
        record.parameters.color2[i] = options.domain_max[i];
    }
    record.parameters.reserved[0] = options.size;
    record.parameters.offsets[0] = options.domain == ColorEncoding::linear;
    op.data(record, options.values, "values");
    op.append({record});
}

void Commands::color_balance(const Image& destination, const ColorBalanceOptions& options) {
    Operation op(recording_.get(), "color_balance");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& pixels = op.resource(destination.resource_, "destination");
    auto record = kernel_record(Kernel::color_balance, pixels);
    std::array<float, 9> values{};
    const std::array tones{options.shadows, options.midtones, options.highlights};
    const std::array names{"shadows", "midtones", "highlights"};
    for (std::size_t t = 0; t < 3; ++t) {
        for (std::size_t c = 0; c < 3; ++c) {
            range(op, tones[t][c], -1, 1, names[t]);
            values[3 * t + c] = tones[t][c];
        }
    }
    record.parameters.reserved[0] = options.preserve_luminosity;
    if (std::ranges::all_of(values, [](float v) { return v == 0; })) {
        return;
    }
    op.data(record, std::span<const float>(values), "tones");
    op.append({record});
}

void Commands::hue_saturation(const Image& destination, const HueSaturationOptions& options) {
    Operation op(recording_.get(), "hue_saturation");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& pixels = op.resource(destination.resource_, "destination");
    auto record = kernel_record(Kernel::hue_saturation, pixels);
    range(op, options.hue, -180, options.colorize ? 360 : 180, "hue");
    range(op, options.saturation, options.colorize ? 0 : -1, 1, "saturation");
    range(op, options.lightness, -1, 1, "lightness");
    record.parameters.values = {options.hue / 360, options.saturation, options.lightness, 0};
    record.parameters.reserved[0] = options.colorize;
    if (!options.colorize && options.hue == 0 && options.saturation == 0 &&
        options.lightness == 0) {
        return;
    }
    op.append({record});
}

void Commands::color_matrix(const Image& destination, const ColorMatrixOptions& options) {
    Operation op(recording_.get(), "color_matrix");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& pixels = op.resource(destination.resource_, "destination");
    auto record = kernel_record(Kernel::color_matrix, pixels);
    domain(op, options.domain);
    for (float x : options.matrix) {
        range(op, x, -65536, 65536, "matrix");
    }
    record.parameters.offsets[0] = options.domain == ColorEncoding::linear;
    if (options.matrix == ColorMatrixOptions{}.matrix) {
        return;
    }
    op.data(record, std::span<const float>(options.matrix), "matrix");
    op.append({record});
}

void Commands::channel_mixer(const Image& destination, const ChannelMixerOptions& options) {
    Operation op(recording_.get(), "channel_mixer");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& pixels = op.resource(destination.resource_, "destination");
    auto record = kernel_record(Kernel::channel_mixer, pixels);
    std::array<float, 12> values{};
    const std::array rows{options.red, options.green, options.blue};
    const std::array names{"red", "green", "blue"};
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 4; ++c) {
            range(op, rows[r][c], -2, 2, names[r]);
            values[4 * r + c] = rows[r][c];
        }
    }
    const ChannelMixerOptions neutral;
    if (!options.monochrome && options.red == neutral.red && options.green == neutral.green &&
        options.blue == neutral.blue) {
        return;
    }
    record.parameters.reserved[0] = options.monochrome;
    op.data(record, std::span<const float>(values), "channels");
    op.append({record});
}

void Commands::black_white(const Image& destination, const BlackWhiteOptions& options) {
    Operation op(recording_.get(), "black_white");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& pixels = op.resource(destination.resource_, "destination");
    auto record = kernel_record(Kernel::black_white, pixels);
    for (float x : options.weights) {
        range(op, x, -2, 3, "weights");
    }
    opaque(op, options.tint_color, "tint_color");
    record.parameters.color1 = rgba(options.tint_color);
    record.parameters.reserved[0] = options.tint;
    op.data(record, std::span<const float>(options.weights), "weights");
    op.append({record});
}

void Commands::photo_filter(const Image& destination, const PhotoFilterOptions& options) {
    Operation op(recording_.get(), "photo_filter");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& pixels = op.resource(destination.resource_, "destination");
    auto record = kernel_record(Kernel::photo_filter, pixels);
    opaque(op, options.color, "color");
    range(op, options.density, 0, 1, "density");
    if (options.density == 0 ||
        (options.color.r == 1 && options.color.g == 1 && options.color.b == 1)) {
        return;
    }
    record.parameters.color1 = rgba(options.color);
    record.parameters.values[0] = options.density;
    record.parameters.reserved[0] = options.preserve_luminosity;
    op.append({record});
}

void Commands::posterize(const Image& destination, const PosterizeOptions& options) {
    Operation op(recording_.get(), "posterize");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& pixels = op.resource(destination.resource_, "destination");
    auto record = kernel_record(Kernel::posterize, pixels);
    op.require(options.levels >= 2 && options.levels <= 256, "levels",
               "levels must lie in [2, 256]");
    record.parameters.values[0] = static_cast<float>(options.levels);
    op.append({record});
}

void Commands::gradient_map(const Image& destination, const GradientMapOptions& options) {
    Operation op(recording_.get(), "gradient_map");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    const auto& pixels = op.resource(destination.resource_, "destination");
    auto record = kernel_record(Kernel::gradient_map, pixels);
    op.require(options.stops.size() >= 2 && options.stops.size() <= 4096, "stops",
               "expected 2..4096 stops");
    for (std::size_t i = 0; i < options.stops.size(); ++i) {
        const auto stop = options.stops[i];
        range(op, stop.position, 0, 1, "stops");
        require_color(op, stop.color, "stops");
        range(op, stop.color.r, 0, stop.color.a, "stops");
        range(op, stop.color.g, 0, stop.color.a, "stops");
        range(op, stop.color.b, 0, stop.color.a, "stops");
        op.require(i == 0 || stop.position >= options.stops[i - 1].position, "stops",
                   "stop positions must be nondecreasing");
    }
    auto words = op.data(record, options.stops.size() * 7, "stops");
    for (std::size_t i = 0; i < options.stops.size(); ++i) {
        const auto stop = options.stops[i];
        auto encode = [&](float v) {
            const double c = stop.color.a > 0 ? double(v) / stop.color.a : 0;
            return float((c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055) *
                         stop.color.a);
        };
        auto decode = [](double c) {
            return float(c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4));
        };
        const std::array values{
            stop.position, encode(stop.color.r),  encode(stop.color.g),       encode(stop.color.b),
            stop.color.a,  decode(stop.position), decode(1.0 - stop.position)};
        for (std::size_t j = 0; j < values.size(); ++j) {
            words[7 * i + j] = std::bit_cast<std::uint32_t>(values[j]);
        }
    }
    record.parameters.reserved = {static_cast<std::uint32_t>(options.stops.size()), options.reverse,
                                  options.dither, 0};
    op.append({record});
}
} // namespace wgpupixel
