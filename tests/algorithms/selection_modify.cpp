#include "selection_support.h"
#include <limits>

using namespace wgpupixel;
using selection::Bytes;

namespace {
constexpr double infinity = std::numeric_limits<double>::infinity();

// Brute-force Euclidean distance from each pixel center to the nearest pixel center
// whose coverage is on the requested side of 50%.
std::vector<double> distances(const Bytes& mask, int width, int height, bool outside) {
    std::vector<double> result(mask.size(), infinity);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            for (int j = 0; j < height; ++j) {
                for (int i = 0; i < width; ++i) {
                    if ((mask[j * width + i] >= 128) != outside) {
                        result[y * width + x] =
                            std::min(result[y * width + x], std::hypot(x - i, y - j));
                    }
                }
            }
        }
    }
    return result;
}

// Apply effect at canvas bounds: area of pixel (x, y) inside the canvas inset by radius.
double inset(int x, int y, int width, int height, double radius) {
    const auto overlap = [&](int i, int extent) {
        return std::clamp(std::min<double>(i + 1, extent - radius) - std::max<double>(i, radius),
                          0.0, 1.0);
    };
    return overlap(x, width) * overlap(y, height);
}

// Hard selections: pixel squares, with the edge halfway between pixel centers.
Bytes expanded(const Bytes& mask, int width, int height, double radius) {
    const auto inside = distances(mask, width, height, false);
    Bytes result(mask.size());
    for (std::size_t i = 0; i < mask.size(); ++i) {
        result[i] = std::max(mask[i], selection::quantize(radius + 1 - inside[i]));
    }
    return result;
}

std::vector<double> shrunk(const Bytes& mask, int width, int height, double radius, bool bounds) {
    const auto outside = distances(mask, width, height, true);
    std::vector<double> result(mask.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int i = y * width + x;
            result[i] = std::clamp(outside[i] - radius, 0.0, 1.0);
            if (bounds) {
                result[i] = std::min(result[i], inset(x, y, width, height, radius));
            }
        }
    }
    return result;
}

Bytes contracted(const Bytes& mask, int width, int height, double radius, bool bounds) {
    const auto kept = shrunk(mask, width, height, radius, bounds);
    Bytes result(mask.size());
    for (std::size_t i = 0; i < mask.size(); ++i) {
        result[i] = std::min(mask[i], selection::quantize(kept[i]));
    }
    return result;
}

// Border: expanded minus contracted by half the width.
Bytes bordered(const Bytes& mask, int width, int height, double band, bool bounds) {
    const auto inside = distances(mask, width, height, false);
    const auto kept = shrunk(mask, width, height, band / 2, bounds);
    Bytes result(mask.size());
    for (std::size_t i = 0; i < mask.size(); ++i) {
        const double grown = std::clamp(band / 2 + 1 - inside[i], 0.0, 1.0);
        result[i] = selection::quantize(grown - kept[i]);
    }
    return result;
}

// Separable filter through the 16-bit intermediate; box sums are exact integers.
Bytes filtered(const Bytes& mask, int width, int height, bool box, double radius, bool bounds) {
    const int taps = box ? int(radius) : int(std::ceil(3 * radius));
    std::vector<float> weights(taps + 1, 1.0f);
    if (!box) {
        std::vector<double> exact(taps + 1);
        double total = 0;
        for (int i = 0; i <= taps; ++i) {
            exact[i] = std::exp(-0.5 * (i / radius) * (i / radius));
            total += i ? 2 * exact[i] : exact[i];
        }
        for (int i = 0; i <= taps; ++i) {
            weights[i] = float(exact[i] / total);
        }
    }
    const auto at = [&](int x, int y, const auto& values) -> double {
        if (bounds && (x < 0 || y < 0 || x >= width || y >= height)) {
            return 0;
        }
        return values[std::clamp(y, 0, height - 1) * width + std::clamp(x, 0, width - 1)];
    };
    std::vector<double> sums(mask.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double sum = 0;
            for (int d = -taps; d <= taps; ++d) {
                sum += weights[std::abs(d)] * at(x + d, y, mask);
            }
            sums[y * width + x] =
                box ? sum : std::floor(std::clamp(sum / 255, 0.0, 1.0) * 65535 + 0.5);
        }
    }
    Bytes result(mask.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double sum = 0;
            for (int d = -taps; d <= taps; ++d) {
                sum += weights[std::abs(d)] * at(x, y + d, sums);
            }
            const double side = 2 * taps + 1;
            result[y * width + x] = selection::quantize(
                box ? (sum / (side * side * 255) - 0.5) * side + 0.5 : sum / 65535);
        }
    }
    return result;
}
// Direct separable Gaussian convolution in double precision: edges repeat, or the
// outside is unselected with canvas bounds.
Bytes direct_gaussian(const Bytes& input, int width, int height, double sigma, bool bounds) {
    const int reach = int(std::ceil(4 * sigma));
    std::vector<double> kernel(reach + 1);
    double total = 0;
    for (int i = 0; i <= reach; ++i) {
        kernel[i] = std::exp(-0.5 * (i / sigma) * (i / sigma));
        total += i ? 2 * kernel[i] : kernel[i];
    }
    const auto at = [&](const std::vector<double>& v, int x, int y) {
        if (bounds && (x < 0 || y < 0 || x >= width || y >= height)) {
            return 0.0;
        }
        return v[std::clamp(y, 0, height - 1) * width + std::clamp(x, 0, width - 1)];
    };
    std::vector<double> source(input.begin(), input.end()), rows(input.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double sum = 0;
            for (int d = -reach; d <= reach; ++d) {
                sum += kernel[std::abs(d)] * at(source, x + d, y);
            }
            rows[y * width + x] = sum / total;
        }
    }
    Bytes result(input.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double sum = 0;
            for (int d = -reach; d <= reach; ++d) {
                sum += kernel[std::abs(d)] * at(rows, x, y + d);
            }
            result[y * width + x] = selection::quantize(sum / total / 255);
        }
    }
    return result;
}
} // namespace

int main() {
    test::run("expand, contract and border follow exact Euclidean distances", [] {
        auto ctx = Context::create();
        for (const auto size :
             {std::array{37, 29}, std::array{5, 1}, std::array{1, 23}, std::array{9, 150}}) {
            const int width = size[0], height = size[1];
            auto mask = ctx.create_mask({width, height});
            auto scratch = ctx.create_workspace(expand_requirements({width, height}, {.radius = 1}).workspace);
            auto band = ctx.create_workspace(border_requirements({width, height}, {.width = 1}).workspace);
            test::check(contract_requirements({width, height}, {.radius = 1}).workspace.bytes() ==
                            expand_requirements({width, height}, {.radius = 3}).workspace.bytes(),
                        "expand and contract share workspace sizes");
            {
                // Hard selections: the edge lies halfway between pixel centers.
                const auto input = selection::blobs(width, height, 17);
                for (const float radius : {1.0f, 2.5f, 6.0f, 40.0f, 90.0f}) {
                    selection::upload(ctx, mask, input);
                    ctx.run_and_wait(
                        [&](Commands& cmd) { cmd.expand(mask, {.radius = radius, .workspace = scratch}); });
                    selection::expect(selection::read(ctx, mask),
                                      expanded(input, width, height, radius), 1, "expand", width);
                    for (const bool bounds : {false, true}) {
                        selection::upload(ctx, mask, input);
                        ctx.run_and_wait([&](Commands& cmd) {
                            cmd.contract(mask, {.radius = radius, .canvas_bounds = bounds, .workspace = scratch});
                        });
                        selection::expect(selection::read(ctx, mask),
                                          contracted(input, width, height, radius, bounds), 1,
                                          "contract", width);
                        selection::upload(ctx, mask, input);
                        ctx.run_and_wait([&](Commands& cmd) {
                            cmd.border(mask, {.width = radius, .canvas_bounds = bounds, .workspace = band});
                        });
                        selection::expect(selection::read(ctx, mask),
                                          bordered(input, width, height, radius, bounds), 1,
                                          "border", width);
                    }
                }
            }
        }
    });
    test::run("expand, contract and border move anti-aliased edges continuously", [] {
        // Independent truth: exact area coverage of straight edges and discs moved by
        // the radius, not the distance model of the kernel.
        auto ctx = Context::create();
        const auto run = [&](const Mask& mask, const Workspace& scratch, const Bytes& input, int op,
                             float radius) {
            selection::upload(ctx, mask, input);
            ctx.run_and_wait([&](Commands& cmd) {
                if (op == 0) {
                    cmd.expand(mask, {.radius = radius, .workspace = scratch});
                }
                if (op == 1) {
                    cmd.contract(mask, {.radius = radius, .workspace = scratch});
                }
                if (op == 2) {
                    cmd.border(mask, {.width = radius, .workspace = scratch});
                }
            });
            return selection::read(ctx, mask);
        };
        // The reviewer's cases: a tiny radius keeps partial coverage.
        {
            auto mask = ctx.create_mask({4, 1});
            auto scratch = ctx.create_workspace(border_requirements({4, 1}, {.width = 1}).workspace);
            auto distance = ctx.create_workspace(expand_requirements({4, 1}, {.radius = 1}).workspace);
            selection::expect(run(mask, distance, {0, 128, 255, 255}, 0, 0.001f),
                              Bytes{0, 128, 255, 255}, 0, "tiny expand");
            selection::expect(run(mask, distance, {0, 127, 255, 255}, 1, 0.001f),
                              Bytes{0, 127, 255, 255}, 0, "tiny contract");
            (void)scratch;
        }
        // Vertical and horizontal straight edges, both sides selected, several workgroups.
        for (const bool vertical : {true, false}) {
            const int width = vertical ? 300 : 40, height = vertical ? 40 : 300;
            auto mask = ctx.create_mask({width, height});
            auto distance = ctx.create_workspace(expand_requirements({width, height}, {.radius = 1}).workspace);
            auto band = ctx.create_workspace(border_requirements({width, height}, {.width = 1}).workspace);
            for (const double edge : {100.3, 150.5, 200.77}) {
                for (const bool left : {true, false}) {
                    // Coverage of the half-plane {t < edge} (or t > edge) over [i, i + 1).
                    const auto half = [&](double position) {
                        Bytes bytes(std::size_t(width) * height);
                        for (int y = 0; y < height; ++y) {
                            for (int x = 0; x < width; ++x) {
                                const int i = vertical ? x : y;
                                const double inside = left ? position - i : i + 1 - position;
                                bytes[y * width + x] = selection::quantize(inside);
                            }
                        }
                        return bytes;
                    };
                    const auto input = half(edge);
                    const double grow = left ? 1 : -1;
                    for (const float r : {0.001f, 0.25f, 0.5f, 1.7f, 12.2f}) {
                        selection::expect(run(mask, distance, input, 0, r), half(edge + grow * r),
                                          1, "expanded straight edge", width);
                        selection::expect(run(mask, distance, input, 1, r), half(edge - grow * r),
                                          1, "contracted straight edge", width);
                        const auto outer = half(edge + grow * r / 2);
                        const auto inner = half(edge - grow * r / 2);
                        Bytes ring(outer.size());
                        for (std::size_t i = 0; i < ring.size(); ++i) {
                            ring[i] = std::uint8_t(outer[i] - inner[i]);
                        }
                        selection::expect(run(mask, band, input, 2, r), ring, 2,
                                          "border of straight edge", width);
                    }
                }
            }
        }
        // Diagonal edges and discs: exact clipped areas of the moved shapes. The model
        // places the edge from pixel coverage, which for curved or slanted edges is
        // within a few percent of a pixel.
        const int width = 300, height = 200;
        auto mask = ctx.create_mask({width, height});
        auto distance = ctx.create_workspace(expand_requirements({width, height}, {.radius = 1}).workspace);
        const auto disc = [&](double radius) {
            selection::Polygon polygon;
            for (int i = 0; i < 2048; ++i) {
                const double angle = 2 * 3.14159265358979323846 * i / 2048;
                polygon.push_back(
                    {150.2 + radius * std::cos(angle), 100.7 + radius * std::sin(angle)});
            }
            return selection::rasterize(polygon, width, height);
        };
        const auto slope = [&](double offset) {
            // Half-plane x + 0.6 y < offset.
            const selection::Polygon polygon{
                {-10, -10}, {offset + 6, -10}, {offset - 0.6 * 210, 210}, {-10, 210}};
            return selection::rasterize(polygon, width, height);
        };
        for (const float r : {0.3f, 4.5f, 21.0f}) {
            selection::expect(run(mask, distance, disc(40.3), 0, r), disc(40.3 + r), 20,
                              "expanded disc", width);
            selection::expect(run(mask, distance, disc(40.3), 1, r), disc(40.3 - r), 20,
                              "contracted disc", width);
            // The selection ends at the canvas, so compare away from the top and bottom.
            const double shift = r * std::sqrt(1 + 0.36);
            const auto interior = [&](const Bytes& actual, const Bytes& expected,
                                      std::string_view what) {
                const int margin = int(r) + 3;
                const auto rows = std::size_t(height - 2 * margin) * width;
                selection::expect(std::span(actual).subspan(margin * width, rows),
                                  std::span(expected).subspan(margin * width, rows), 20, what,
                                  width);
            };
            interior(run(mask, distance, slope(170.4), 0, r), slope(170.4 + shift),
                     "expanded slanted edge");
            interior(run(mask, distance, slope(170.4), 1, r), slope(170.4 - shift),
                     "contracted slanted edge");
        }
        // The reviewer's case: the corner pixel keeps a quarter, the inset's area.
        {
            auto square = ctx.create_mask({8, 8});
            auto work = ctx.create_workspace(contract_requirements({8, 8}, {.radius = 1}).workspace);
            selection::upload(ctx, square, Bytes(64, 255));
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.contract(square, {.radius = 2.5f, .canvas_bounds = true, .workspace = work});
            });
            const auto actual = selection::read(ctx, square);
            test::check(actual[2 * 8 + 2] == 64 && actual[2 * 8 + 4] == 128 &&
                            actual[4 * 8 + 4] == 255 && actual[1 * 8 + 4] == 0,
                        "canvas-bounds contract corner area");
        }
        // Apply effect at canvas bounds insets select-all by exactly the radius.
        for (const float r : {3.0f, 2.5f}) {
            Bytes all(std::size_t(width) * height, 255);
            selection::upload(ctx, mask, all);
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.contract(mask, {.radius = r, .canvas_bounds = true, .workspace = distance});
            });
            const auto actual = selection::read(ctx, mask);
            const auto overlap = [&](int i, int extent) {
                return std::clamp(std::min<double>(i + 1, extent - r) - std::max<double>(i, r), 0.0,
                                  1.0);
            };
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const double cx = overlap(x, width), cy = overlap(y, height);
                    const double expected = cx * cy; // Area of the inset rectangle.
                    test::check(std::abs(actual[y * width + x] - selection::quantize(expected)) <=
                                    1,
                                "canvas-bounds contract");
                }
            }
        }
    });
    test::run("distance transform spans long rows, columns and empty selections", [] {
        auto ctx = Context::create();
        const int width = 300, height = 7;
        auto mask = ctx.create_mask({width, height});
        auto scratch = ctx.create_workspace(expand_requirements({width, height}, {.radius = 1}).workspace);
        Bytes input(width * height, 0);
        input[3 * width + 5] = 255;
        input[6 * width + 290] = 255;
        selection::upload(ctx, mask, input);
        ctx.run_and_wait([&](Commands& cmd) { cmd.expand(mask, {.radius = 250, .workspace = scratch}); });
        selection::expect(selection::read(ctx, mask), expanded(input, width, height, 250), 1,
                          "long expand", width);
        Bytes all(width * height, 255);
        selection::upload(ctx, mask, all);
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.contract(mask, {.radius = 2, .canvas_bounds = true, .workspace = scratch});
        });
        selection::expect(selection::read(ctx, mask), contracted(all, width, height, 2, true), 1,
                          "canvas contract", width);
        selection::upload(ctx, mask, all);
        ctx.run_and_wait([&](Commands& cmd) { cmd.contract(mask, {.radius = 2, .workspace = scratch}); });
        selection::expect(selection::read(ctx, mask), all, 0, "select all stays", width);
        Bytes none(width * height, 0);
        selection::upload(ctx, mask, none);
        ctx.run_and_wait([&](Commands& cmd) { cmd.expand(mask, {.radius = 9, .workspace = scratch}); });
        selection::expect(selection::read(ctx, mask), none, 0, "empty stays empty", width);
    });
    test::run("feather and smooth match separable references", [] {
        auto ctx = Context::create();
        for (const auto size : {std::array{29, 23}, std::array{3, 17}, std::array{19, 1}}) {
            const int width = size[0], height = size[1];
            auto mask = ctx.create_mask({width, height});
            auto scratch = ctx.create_workspace(feather_requirements({width, height}, {.radius = 1}).workspace);
            auto box = ctx.create_workspace(smooth_requirements({width, height}, {.radius = 1}).workspace);
            const auto input = selection::blobs(width, height, 5, true);
            for (const bool bounds : {false, true}) {
                for (const float radius : {0.4f, 1.5f, 4.0f}) {
                    selection::upload(ctx, mask, input);
                    ctx.run_and_wait([&](Commands& cmd) {
                        cmd.feather(mask, {.radius = radius, .canvas_bounds = bounds, .workspace = scratch});
                    });
                    selection::expect(selection::read(ctx, mask),
                                      filtered(input, width, height, false, radius, bounds), 1,
                                      "feather", width);
                }
                const auto hard = selection::blobs(width, height, 9);
                for (const int radius : {1, 2, 5}) {
                    selection::upload(ctx, mask, hard);
                    ctx.run_and_wait([&](Commands& cmd) {
                        cmd.smooth(mask, {.radius = radius, .canvas_bounds = bounds, .workspace = box});
                    });
                    selection::expect(selection::read(ctx, mask),
                                      filtered(hard, width, height, true, radius, bounds), 1,
                                      "smooth", width);
                }
            }
        }
    });
    test::run("large feathers stay Gaussian with bounded cost", [] {
        // Reference: exact separable Gaussian convolution in double precision, a
        // different method from the running-sum boxes used above radius 16.
        auto ctx = Context::create();
        const int width = 420, height = 300;
        auto mask = ctx.create_mask({width, height});
        const auto required = feather_requirements({width, height}, {.radius = 60});

        auto scratch = ctx.create_workspace(required.workspace);
        Bytes input(std::size_t(width) * height, 0);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const bool inside = (x > 60 && x < 250 && y > 40 && y < 200) ||
                                    std::hypot(x - 330.0, y - 210.0) < 70 || x > 405;
                input[y * width + x] = inside ? 255 : 0;
            }
        }
        const auto gaussian = [&](double sigma, bool bounds) {
            const int reach = int(std::ceil(4 * sigma));
            std::vector<double> kernel(reach + 1);
            double total = 0;
            for (int i = 0; i <= reach; ++i) {
                kernel[i] = std::exp(-0.5 * (i / sigma) * (i / sigma));
                total += i ? 2 * kernel[i] : kernel[i];
            }
            const auto at = [&](const std::vector<double>& v, int x, int y) {
                if (bounds && (x < 0 || y < 0 || x >= width || y >= height)) {
                    return 0.0;
                }
                return v[std::clamp(y, 0, height - 1) * width + std::clamp(x, 0, width - 1)];
            };
            std::vector<double> source(input.begin(), input.end()), rows(input.size());
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    double sum = 0;
                    for (int d = -reach; d <= reach; ++d) {
                        sum += kernel[std::abs(d)] * at(source, x + d, y);
                    }
                    rows[y * width + x] = sum / total;
                }
            }
            Bytes result(input.size());
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    double sum = 0;
                    for (int d = -reach; d <= reach; ++d) {
                        sum += kernel[std::abs(d)] * at(rows, x, y + d);
                    }
                    result[y * width + x] = selection::quantize(sum / total / 255);
                }
            }
            return result;
        };
        for (const float radius : {16.5f, 40.0f, 150.0f, 250.0f}) {
            for (const bool bounds : {false, true}) {
                selection::upload(ctx, mask, input);
                ctx.run_and_wait([&](Commands& cmd) {
                    cmd.feather(mask, {.radius = radius, .canvas_bounds = bounds, .workspace = scratch});
                });
                selection::expect(selection::read(ctx, mask), gaussian(radius, bounds), 4,
                                  "large feather", width);
            }
        }
    });
    test::run("large feathers repeat the canvas edge like the direct Gaussian", [] {
        auto ctx = Context::create();
        // The reviewer's case: one selected pixel at the edge of a 64x1 mask.
        {
            auto mask = ctx.create_mask({64, 1});
            Bytes input(64, 0);
            input[0] = 255;
            for (const float radius : {16.0f, 16.5f, 40.0f, 150.0f}) {
                auto scratch =
                    ctx.create_workspace(feather_requirements({64, 1}, {.radius = radius}).workspace);
                for (const bool bounds : {false, true}) {
                    selection::upload(ctx, mask, input);
                    ctx.run_and_wait([&](Commands& cmd) {
                        cmd.feather(mask, {.radius = radius, .canvas_bounds = bounds, .workspace = scratch});
                    });
                    selection::expect(selection::read(ctx, mask),
                                      direct_gaussian(input, 64, 1, radius, bounds), 2,
                                      "edge pixel feather");
                }
                ctx.destroy(scratch);
            }
        }
        // Thin selections along every canvas edge, a corner pixel and an inner line, on a
        // mask of several workgroups, for radii across the switch to the reduced grid.
        const int width = 97, height = 61;
        auto mask = ctx.create_mask({width, height});
        Bytes input(std::size_t(width) * height, 0);
        for (int x = 0; x < width; ++x) {
            input[x] = 255;
            input[(height - 1) * width + x] = x % 3 ? 255 : 0;
        }
        for (int y = 0; y < height; ++y) {
            input[y * width] = y < 30 ? 255 : 0;
            input[y * width + width - 1] = 200;
            input[y * width + 50] = 255;
        }
        for (const float radius : {15.9f, 16.5f, 23.0f, 70.0f, 300.0f}) {
            auto scratch =
                ctx.create_workspace(feather_requirements({width, height}, {.radius = radius}).workspace);
            for (const bool bounds : {false, true}) {
                selection::upload(ctx, mask, input);
                ctx.run_and_wait([&](Commands& cmd) {
                    cmd.feather(mask, {.radius = radius, .canvas_bounds = bounds, .workspace = scratch});
                });
                selection::expect(selection::read(ctx, mask),
                                  direct_gaussian(input, width, height, radius, bounds), 4,
                                  "edge lines feather", width);
            }
            ctx.destroy(scratch);
        }
    });
    test::run("threshold and levels remap every coverage value exactly", [] {
        auto ctx = Context::create();
        auto mask = ctx.create_mask({16, 16});
        Bytes ramp(256);
        for (int i = 0; i < 256; ++i) {
            ramp[i] = std::uint8_t(i);
        }
        for (const float value : {0.0f, 0.5f, 0.73f, 1.0f}) {
            selection::upload(ctx, mask, ramp);
            ctx.run_and_wait([&](Commands& cmd) { cmd.threshold(mask, {.value = value}); });
            Bytes expected(256);
            for (int i = 0; i < 256; ++i) {
                expected[i] = i / 255.0 >= value ? 255 : 0;
            }
            selection::expect(selection::read(ctx, mask), expected, 0, "threshold");
        }
        const MaskLevelsOptions levels{.transfer = {.input_black = 0.2f,
                                                    .input_white = 0.8f,
                                                    .gamma = 1.7f,
                                                    .output_black = 0.1f,
                                                    .output_white = 0.95f}};
        selection::upload(ctx, mask, ramp);
        ctx.run_and_wait([&](Commands& cmd) { cmd.levels(mask, levels); });
        Bytes expected(256);
        for (int i = 0; i < 256; ++i) {
            const double t = std::clamp((i / 255.0 - 0.2f) / (double(0.8f) - 0.2f), 0.0, 1.0);
            expected[i] =
                selection::quantize(0.1f + std::pow(t, 1 / 1.7f) * (double(0.95f) - 0.1f));
        }
        selection::expect(selection::read(ctx, mask), expected, 0, "levels");
        selection::upload(ctx, mask, ramp);
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.levels(mask, {.transfer = {.output_black = 1, .output_white = 0}}); // Inverts.
        });
        for (int i = 0; i < 256; ++i) {
            expected[i] = std::uint8_t(255 - i);
        }
        selection::expect(selection::read(ctx, mask), expected, 0, "inverting levels");
    });
    test::run("modify operations validate workspaces and parameters atomically", [] {
        auto ctx = Context::create();
        auto mask = ctx.create_mask({6, 5});
        auto scratch = ctx.create_workspace(feather_requirements({6, 5}, {.radius = 1}).workspace);
        auto box = ctx.create_workspace(smooth_requirements({6, 5}, {.radius = 1}).workspace);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        // Four distance planes of 4-byte words over 30 pixels: 480 bytes.
        test::check(border_requirements({6, 5}, {.width = 2}).workspace.bytes() == 480,
                    "border workspace size");
        test::check(feather_requirements({6, 5}, {}).workspace.bytes() == 0 &&
                        expand_requirements({6, 5}, {}).workspace.bytes() == 0,
                    "a zero radius needs no workspace");

        auto cmd = ctx.create_commands(4);
        test::error(ErrorCode::capacity, "feather", "workspace",
                    [&] { cmd.feather(mask, {.radius = 1}); });
        test::error(ErrorCode::capacity, "feather", "workspace",
                    [&] { cmd.feather(mask, {.radius = 1, .workspace = box}); });
        auto other = Context::create();
        auto foreign = other.create_workspace(feather_requirements({6, 5}, {.radius = 1}).workspace);
        test::error(ErrorCode::invalid_resource, "feather", "workspace",
                    [&] { cmd.feather(mask, {.radius = 1, .workspace = foreign}); });
        test::error(ErrorCode::invalid_argument, "feather", "radius",
                    [&] { cmd.feather(mask, {.radius = nan, .workspace = scratch}); });
        test::error(ErrorCode::invalid_argument, "smooth", "radius",
                    [&] { cmd.smooth(mask, {.radius = 101, .workspace = scratch}); });
        test::error(ErrorCode::invalid_argument, "expand", "radius",
                    [&] { cmd.expand(mask, {.radius = -1, .workspace = scratch}); });
        test::error(ErrorCode::capacity, "expand", "workspace",
                    [&] { cmd.expand(mask, {.radius = 1, .workspace = box}); });
        test::error(ErrorCode::invalid_argument, "border", "width",
                    [&] { cmd.border(mask, {.width = 0, .workspace = scratch}); });
        test::error(ErrorCode::invalid_argument, "levels", "transfer.input_white", [&] {
            cmd.levels(mask, {.transfer = {.input_black = 0.5f, .input_white = 0.5f}});
        });
        test::error(ErrorCode::invalid_argument, "levels", "transfer.gamma",
                    [&] { cmd.levels(mask, {.transfer = {.gamma = 0}}); });
        test::error(ErrorCode::invalid_argument, "threshold", "value",
                    [&] { cmd.threshold(mask, {.value = 2}); });
        test::error(ErrorCode::invalid_argument, "feather_requirements", "radius",
                    [] { (void)feather_requirements({1, 1}, {.radius = 2000}); });
        test::error(ErrorCode::invalid_argument, "expand_requirements", "mask",
                    [] { (void)expand_requirements({0, 1}, {}); });
        // Oversized dimensions are rejected before any scratch arithmetic can overflow.
        constexpr ImageSize huge{2147483647, 2147483647};
        test::error(ErrorCode::capacity, "expand_requirements", "mask",
                    [&] { (void)expand_requirements(huge, {.radius = 1}); });
        test::error(ErrorCode::capacity, "contract_requirements", "mask",
                    [&] { (void)contract_requirements(huge, {.radius = 1}); });
        test::error(ErrorCode::capacity, "border_requirements", "mask",
                    [&] { (void)border_requirements(huge, {.width = 1}); });
        test::error(ErrorCode::capacity, "feather_requirements", "mask",
                    [&] { (void)feather_requirements(huge, {.radius = 1}); });
        test::error(ErrorCode::capacity, "smooth_requirements", "mask",
                    [&] { (void)smooth_requirements(huge, {.radius = 1}); });
        test::error(ErrorCode::capacity, "select_magic_wand_requirements", "source",
                    [&] { (void)select_magic_wand_requirements(huge, {}); });
        // The largest accepted dimensions give positive, consistent scratch sizes.
        const auto wide = border_requirements({65536, 65535}, {.width = 1}).workspace.bytes();
        test::check(wide == 16ull * 65536 * 65535, "border workspace size");
        test::error(ErrorCode::invalid_argument, "expand", "radius",
                    [&] { cmd.expand(mask, {.radius = 2001, .workspace = scratch}); });
        test::error(ErrorCode::invalid_argument, "border", "width",
                    [&] { cmd.border(mask, {.width = 4001, .workspace = scratch}); });
        // Zero radii record nothing.
        cmd.feather(mask, {.workspace = scratch});
        cmd.smooth(mask, {.workspace = box});
        const auto input = selection::random_bytes(30, 4);
        selection::upload(ctx, mask, input);
        ctx.submit_and_wait(cmd);
        selection::expect(selection::read(ctx, mask), input, 0, "no-op");
    });
    return test::finish();
}
