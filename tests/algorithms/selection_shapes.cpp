#include "selection_support.h"
#include <limits>
#include <numbers>

using namespace wgpupixel;
using selection::Bytes;
using selection::Polygon;

namespace {
constexpr std::array modes{SelectionMode::replace, SelectionMode::add, SelectionMode::subtract,
                           SelectionMode::intersect, SelectionMode::difference};

Polygon transformed(Polygon polygon, const Affine& m) {
    for (auto& p : polygon) {
        p = {m.a * p[0] + m.c * p[1] + m.e, m.b * p[0] + m.d * p[1] + m.f};
    }
    return polygon;
}

Polygon ellipse(double x, double y, double width, double height, int segments) {
    Polygon result;
    const double rx = width / 2, ry = height / 2;
    // Equal-area polygon: the vertex radius compensates for chord shrinkage.
    const double outward =
        std::sqrt(2 * std::numbers::pi / segments / std::sin(2 * std::numbers::pi / segments));
    for (int i = 0; i < segments; ++i) {
        const double angle = 2 * std::numbers::pi * i / segments;
        result.push_back(
            {x + rx + outward * rx * std::cos(angle), y + ry + outward * ry * std::sin(angle)});
    }
    return result;
}

Polygon rounded(double x, double y, double width, double height, double radius) {
    Polygon result;
    const std::array<std::array<double, 2>, 4> centers{{{x + width - radius, y + radius},
                                                        {x + width - radius, y + height - radius},
                                                        {x + radius, y + height - radius},
                                                        {x + radius, y + radius}}};
    for (int corner = 0; corner < 4; ++corner) {
        for (int i = 0; i <= 512; ++i) {
            const double angle = (corner - 1 + i / 512.0) * std::numbers::pi / 2;
            result.push_back({centers[corner][0] + radius * std::cos(angle),
                              centers[corner][1] + radius * std::sin(angle)});
        }
    }
    return result;
}

std::vector<Point> points_of(const Polygon& polygon) {
    std::vector<Point> points;
    for (const auto& p : polygon) {
        points.push_back({float(p[0]), float(p[1])});
    }
    return points;
}

Polygon as_polygon(std::span<const Point> points) {
    Polygon polygon;
    for (const auto p : points) {
        polygon.push_back({p.x, p.y});
    }
    return polygon;
}

// Separable Gaussian with the library's weights, through the 16-bit intermediate.
Bytes feathered(const Bytes& coverage, int width, int height, double radius) {
    const int taps = int(std::ceil(3 * radius));
    std::vector<double> exact(taps + 1);
    double total = 0;
    for (int i = 0; i <= taps; ++i) {
        exact[i] = std::exp(-0.5 * (i / radius) * (i / radius));
        total += i ? 2 * exact[i] : exact[i];
    }
    std::vector<float> weights;
    for (double w : exact) {
        weights.push_back(float(w / total));
    }
    std::vector<double> sums(coverage.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double sum = 0;
            for (int d = -taps; d <= taps; ++d) {
                sum += weights[std::abs(d)] * coverage[y * width + std::clamp(x + d, 0, width - 1)];
            }
            sums[y * width + x] = std::floor(std::clamp(sum / 255, 0.0, 1.0) * 65535 + 0.5);
        }
    }
    Bytes result(coverage.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double sum = 0;
            for (int d = -taps; d <= taps; ++d) {
                sum += weights[std::abs(d)] * sums[std::clamp(y + d, 0, height - 1) * width + x];
            }
            result[y * width + x] = selection::quantize(sum / 65535);
        }
    }
    return result;
}
// Exact coverage of axis-aligned boxes (x0, y0, x1, y1, winding): the winding is
// constant on the grid of all box coordinates, so the fill rule integrates exactly
// cell by cell.
Bytes box_coverage(const std::vector<std::array<double, 5>>& boxes, FillRule rule, int width,
                   int height) {
    Bytes expected(std::size_t(width) * height);
    for (int y = 0; y < height; ++y) {
        std::vector<const std::array<double, 5>*> row;
        for (const auto& b : boxes) {
            if (b[3] > y && b[1] < y + 1) {
                row.push_back(&b);
            }
        }
        for (int x = 0; x < width; ++x) {
            std::vector<double> xs{double(x), x + 1.0}, ys{double(y), y + 1.0};
            for (const auto* b : row) {
                for (double v : {(*b)[0], (*b)[2]}) {
                    if (v > x && v < x + 1) {
                        xs.push_back(v);
                    }
                }
                for (double v : {(*b)[1], (*b)[3]}) {
                    if (v > y && v < y + 1) {
                        ys.push_back(v);
                    }
                }
            }
            std::ranges::sort(xs);
            std::ranges::sort(ys);
            double area = 0;
            for (std::size_t i = 0; i + 1 < xs.size(); ++i) {
                for (std::size_t j = 0; j + 1 < ys.size(); ++j) {
                    const double mx = (xs[i] + xs[i + 1]) / 2, my = (ys[j] + ys[j + 1]) / 2;
                    int winding = 0;
                    for (const auto* b : row) {
                        if (mx > (*b)[0] && mx < (*b)[2] && my > (*b)[1] && my < (*b)[3]) {
                            winding += int((*b)[4]);
                        }
                    }
                    const bool filled =
                        rule == FillRule::nonzero ? winding != 0 : (winding & 1) != 0;
                    area += filled * (xs[i + 1] - xs[i]) * (ys[j + 1] - ys[j]);
                }
            }
            expected[y * width + x] = selection::quantize(area);
        }
    }
    return expected;
}

// Adds a box contour; clockwise on screen winds +1 inside.
void add_box(std::vector<Point>& points, std::vector<std::uint32_t>& counts,
             std::vector<std::array<double, 5>>& boxes, float x0, float y0, float x1, float y1,
             bool clockwise) {
    if (clockwise) {
        points.insert(points.end(), {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}});
    } else {
        points.insert(points.end(), {{x0, y0}, {x0, y1}, {x1, y1}, {x1, y0}});
    }
    counts.push_back(4);
    boxes.push_back({x0, y0, x1, y1, clockwise ? 1.0 : -1.0});
}
} // namespace

int main() {
    test::run("rectangle marquee has exact area coverage at fractional edges", [] {
        auto ctx = Context::create();
        for (const auto size : {std::array{13, 11}, std::array{1, 1}, std::array{67, 5}}) {
            auto mask = ctx.create_mask({size[0], size[1]});
            for (const auto box :
                 {std::array{2.3f, 1.7f, 7.45f, 5.2f}, std::array{-3.5f, -1.0f, 9.0f, 4.0f},
                  std::array{0.25f, 0.25f, 0.5f, 0.25f}}) {
                ctx.run_and_wait([&](Commands& cmd) {
                    cmd.select_rectangle(
                        mask, {.origin = {box[0], box[1]}, .width = box[2], .height = box[3]});
                });
                Bytes expected(std::size_t(size[0]) * size[1]);
                for (int y = 0; y < size[1]; ++y) {
                    for (int x = 0; x < size[0]; ++x) {
                        const double cx = std::max(0.0, std::min<double>(x + 1, box[0] + box[2]) -
                                                            std::max<double>(x, box[0]));
                        const double cy = std::max(0.0, std::min<double>(y + 1, box[1] + box[3]) -
                                                            std::max<double>(y, box[1]));
                        expected[y * size[0] + x] = selection::quantize(cx * cy);
                    }
                }
                selection::expect(selection::read(ctx, mask), expected, 1, "rectangle", size[0]);
            }
        }
    });
    test::run("transformed rectangle, ellipse and rounded rectangle match clipped areas", [] {
        auto ctx = Context::create();
        const int width = 41, height = 37;
        auto mask = ctx.create_mask({width, height});
        const auto rotation = Affine::rotate(27, {20, 18}) * Affine::scale(1.1f, 0.9f);
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.select_rectangle(
                mask,
                {.origin = {6.5f, 9.25f}, .width = 27, .height = 15.5f, .transform = rotation});
        });
        const Polygon box{{6.5, 9.25}, {33.5, 9.25}, {33.5, 24.75}, {6.5, 24.75}};
        selection::expect(selection::read(ctx, mask),
                          selection::rasterize(transformed(box, rotation), width, height), 1,
                          "rotated rectangle", width);
        for (const auto shape :
             {std::array{3.2f, 4.6f, 33.1f, 27.9f}, std::array{10.0f, 12.0f, 1.5f, 3.0f},
              std::array{-8.0f, -6.0f, 30.0f, 22.0f}}) {
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.select_ellipse(
                    mask, {.origin = {shape[0], shape[1]}, .width = shape[2], .height = shape[3]});
            });
            selection::expect(
                selection::read(ctx, mask),
                selection::rasterize(ellipse(shape[0], shape[1], shape[2], shape[3], 4096), width,
                                     height),
                2, "ellipse", width);
        }
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.select_ellipse(
                mask, {.origin = {5, 7}, .width = 24, .height = 14, .transform = rotation});
        });
        selection::expect(
            selection::read(ctx, mask),
            selection::rasterize(transformed(ellipse(5, 7, 24, 14, 4096), rotation), width, height),
            2, "rotated ellipse", width);
        for (const float radius : {5.5f, 100.0f}) {
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.select_rectangle(mask, {.origin = {2.5f, 3.75f},
                                            .width = 34,
                                            .height = 29.5f,
                                            .corner_radius = radius});
            });
            selection::expect(
                selection::read(ctx, mask),
                selection::rasterize(rounded(2.5, 3.75, 34, 29.5, std::min(radius, 14.75f)), width,
                                     height),
                2, "rounded rectangle", width);
        }
    });
    test::run("lasso with thousands of points and concave outline is exact", [] {
        auto ctx = Context::create();
        const int width = 97, height = 83;
        auto mask = ctx.create_mask({width, height});
        Polygon lasso;
        for (int i = 0; i < 3000; ++i) {
            const double angle = 2 * std::numbers::pi * i / 3000;
            const double radius = 30 + 9 * std::sin(7 * angle) + 3 * std::cos(19 * angle);
            lasso.push_back({48.3 + radius * std::cos(angle), 41.7 + radius * std::sin(angle)});
        }
        const auto points = points_of(lasso);
        for (const auto rule : {FillRule::nonzero, FillRule::even_odd}) {
            ctx.run_and_wait(
                [&](Commands& cmd) { cmd.select_polygon(mask, {.points = points, .rule = rule}); });
            selection::expect(selection::read(ctx, mask),
                              selection::rasterize(as_polygon(points), width, height), 1, "lasso",
                              width);
        }
        // Counterclockwise orientation selects the same area.
        std::vector<Point> reversed(points.rbegin(), points.rend());
        ctx.run_and_wait([&](Commands& cmd) { cmd.select_polygon(mask, {.points = reversed}); });
        selection::expect(selection::read(ctx, mask),
                          selection::rasterize(as_polygon(points), width, height), 1,
                          "reversed lasso", width);
    });
    test::run("fill rules resolve self-intersections, holes and islands", [] {
        auto ctx = Context::create();
        const int width = 64, height = 60;
        auto mask = ctx.create_mask({width, height});
        std::vector<Point> star;
        for (int i = 0; i < 5; ++i) {
            const double angle = -std::numbers::pi / 2 + i * 4 * std::numbers::pi / 5;
            star.push_back(
                {float(32.13 + 28 * std::cos(angle)), float(31.29 + 28 * std::sin(angle))});
        }
        const std::vector<Polygon> star_contours{as_polygon(star)};
        for (const auto rule : {FillRule::nonzero, FillRule::even_odd}) {
            const auto selected = [&](double x, double y) {
                const int w = selection::winding(star_contours, x, y);
                return rule == FillRule::nonzero ? w != 0 : (w & 1) != 0;
            };
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.select_polygon(mask, {.points = star, .rule = rule, .anti_alias = false});
            });
            Bytes centers(width * height);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    centers[y * width + x] = selected(x + 0.5, y + 0.5) ? 255 : 0;
                }
            }
            selection::expect(selection::read(ctx, mask), centers, 0, "aliased star", width);
            ctx.run_and_wait(
                [&](Commands& cmd) { cmd.select_polygon(mask, {.points = star, .rule = rule}); });
            // 32x32 supersampled reference; exact wherever a pixel is uniform.
            Bytes sampled(width * height);
            std::vector<bool> uniform(width * height);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    int count = 0;
                    for (int j = 0; j < 32; ++j) {
                        for (int i = 0; i < 32; ++i) {
                            count += selected(x + (i + 0.5) / 32, y + (j + 0.5) / 32);
                        }
                    }
                    sampled[y * width + x] = selection::quantize(count / 1024.0);
                    uniform[y * width + x] = count == 0 || count == 1024;
                }
            }
            // On a pixel row holding a crossing or vertex, the winding beside an edge may
            // change inside one 1/16-row slab; coverage there is within 1/16 of exact.
            std::vector<double> events;
            for (int a = 0; a < 5; ++a) {
                events.push_back(star[a].y);
                for (int b = a + 2; b < 5; ++b) {
                    const auto p = star[a], r = star[(a + 1) % 5], q = star[b],
                               t = star[(b + 1) % 5];
                    const double dx1 = r.x - p.x, dy1 = r.y - p.y, dx2 = t.x - q.x, dy2 = t.y - q.y;
                    const double det = dx1 * dy2 - dy1 * dx2;
                    const double u = ((q.x - p.x) * dy2 - (q.y - p.y) * dx2) / det;
                    events.push_back(p.y + u * dy1);
                }
            }
            const auto actual = selection::read(ctx, mask);
            for (int i = 0; i < width * height; ++i) {
                bool crossing = false;
                for (const double y : events) {
                    crossing |= std::floor(y) == i / width;
                }
                // The supersampled reference itself is within 1/32 on edge pixels.
                const int tolerance = uniform[i] ? (crossing ? 16 : 1) : (crossing ? 20 : 10);
                test::check(std::abs(int(actual[i]) - int(sampled[i])) <= tolerance,
                            "anti-aliased star differs from supersampled reference at " +
                                std::to_string(i % width) + "," + std::to_string(i / width) + ": " +
                                std::to_string(actual[i]) + " vs " + std::to_string(sampled[i]));
            }
            test::check(actual[32 * width + 32] == (rule == FillRule::nonzero ? 255 : 0),
                        "star center follows the fill rule");
        }
        // Outer square with an inner square of the same orientation: winding 2 inside.
        const std::vector<Point> squares{{4, 4},   {60, 4},  {60, 56}, {4, 56},
                                         {20, 20}, {44, 20}, {44, 40}, {20, 40}};
        const std::array<std::uint32_t, 2> counts{4, 4};
        for (const auto rule : {FillRule::nonzero, FillRule::even_odd}) {
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.select_polygon(mask, {.points = squares, .contours = counts, .rule = rule});
            });
            const auto actual = selection::read(ctx, mask);
            test::check(actual[30 * width + 30] == (rule == FillRule::nonzero ? 255 : 0),
                        "hole follows the fill rule");
            test::check(actual[10 * width + 10] == 255 && actual[2 * width + 2] == 0,
                        "outer contour selected");
        }
    });
    test::run("fill rules are exact for overlapping, opposite and duplicated contours", [] {
        auto ctx = Context::create();
        // Reviewer cases: several contours inside one pixel.
        {
            auto mask = ctx.create_mask({4, 1});
            const std::array<std::uint32_t, 2> counts{4, 4};
            const std::array<Point, 8> islands{
                {{0, 0}, {.25f, 0}, {.25f, 1}, {0, 1}, {.75f, 0}, {.75f, 1}, {1, 1}, {1, 0}}};
            for (const auto rule : {FillRule::nonzero, FillRule::even_odd}) {
                ctx.run_and_wait([&](Commands& cmd) {
                    cmd.select_polygon(mask, {.points = islands, .contours = counts, .rule = rule});
                });
                test::check(selection::read(ctx, mask)[0] == 128, "disjoint opposite contours");
            }
            const std::array<Point, 8> nested{
                {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}, {.5f, 0}, {.5f, 1}, {0, 1}}};
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.select_polygon(mask, {.points = nested, .contours = counts});
            });
            test::check(selection::read(ctx, mask)[0] == 255, "nested nonzero");
            const std::array<Point, 8> twice{
                {{0, 0}, {.5f, 0}, {.5f, 1}, {0, 1}, {0, 0}, {.5f, 0}, {.5f, 1}, {0, 1}}};
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.select_polygon(
                    mask, {.points = twice, .contours = counts, .rule = FillRule::even_odd});
            });
            test::check(selection::read(ctx, mask)[0] == 0, "duplicated contour cancels");
        }
        // Random overlapping axis-aligned rectangles of both orientations. Reference: the
        // winding is constant on the grid of all rectangle coordinates, so integrating the
        // fill rule cell by cell is exact.
        const int width = 97, height = 61;
        auto mask = ctx.create_mask({width, height});
        std::mt19937 random(5);
        const auto coordinate = [&](int extent) {
            return float(int(random() % (extent * 8)) - 8) / 8.0f + 0.03125f * float(random() % 4);
        };
        for (int trial = 0; trial < 6; ++trial) {
            std::vector<Point> points;
            std::vector<std::uint32_t> counts;
            std::vector<std::array<double, 5>> boxes; // x0, y0, x1, y1, direction
            for (int k = 0; k < 12; ++k) {
                float x0 = coordinate(width), x1 = coordinate(width);
                float y0 = coordinate(height), y1 = coordinate(height);
                if (x0 == x1 || y0 == y1) {
                    continue;
                }
                if (x0 > x1) {
                    std::swap(x0, x1);
                }
                if (y0 > y1) {
                    std::swap(y0, y1);
                }
                // Tiny boxes too, so several contours share pixels.
                if (k % 3 == 0) {
                    x1 = x0 + 0.25f + 0.125f * float(random() % 4);
                    y1 = y0 + 0.5f;
                }
                const bool clockwise = random() % 2;
                if (clockwise) {
                    points.insert(points.end(), {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}});
                } else {
                    points.insert(points.end(), {{x0, y0}, {x0, y1}, {x1, y1}, {x1, y0}});
                }
                counts.push_back(4);
                // Clockwise on screen: the right edge goes down, winding +1 inside.
                boxes.push_back({x0, y0, x1, y1, clockwise ? 1.0 : -1.0});
            }
            for (const auto rule : {FillRule::nonzero, FillRule::even_odd}) {
                ctx.run_and_wait([&](Commands& cmd) {
                    cmd.select_polygon(mask, {.points = points, .contours = counts, .rule = rule});
                });
                const auto expected = box_coverage(boxes, rule, width, height);
                selection::expect(selection::read(ctx, mask), expected, 1, "overlapping boxes",
                                  width);
            }
        }
    });
    test::run("crowded pixels and rows keep exact fill rules", [] {
        auto ctx = Context::create();
        // Many duplicated half-pixel contours: exact ordering up to 64 pieces per pixel,
        // exact-rule supersampling beyond. Odd counts select half the pixel.
        auto small = ctx.create_mask({4, 1});
        for (const int copies : {26, 27, 70, 71}) {
            std::vector<Point> points;
            std::vector<std::uint32_t> counts;
            std::vector<std::array<double, 5>> boxes;
            for (int i = 0; i < copies; ++i) {
                add_box(points, counts, boxes, 0, 0, 0.5f, 1, true);
            }
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.select_polygon(
                    small, {.points = points, .contours = counts, .rule = FillRule::even_odd});
            });
            const int got = selection::read(ctx, small)[0];
            test::check(got == (copies % 2 ? 128 : 0), std::to_string(copies) +
                                                           " duplicated half-pixel contours gave " +
                                                           std::to_string(got));
        }
        // The reviewer's islands with hundreds of unrelated contours on the same row, off
        // the canvas on both sides.
        {
            std::vector<Point> points{{0, 0},    {.25f, 0}, {.25f, 1}, {0, 1},
                                      {.75f, 0}, {.75f, 1}, {1, 1},    {1, 0}};
            std::vector<std::uint32_t> counts{4, 4};
            std::vector<std::array<double, 5>> boxes;
            for (int i = 0; i < 450; ++i) {
                add_box(points, counts, boxes, 10.0f + i, 0, 10.25f + i, 1, true);
                add_box(points, counts, boxes, -500.0f + i, 0, -499.75f + i, 1, i % 3 == 0);
            }
            for (const auto rule : {FillRule::nonzero, FillRule::even_odd}) {
                ctx.run_and_wait([&](Commands& cmd) {
                    cmd.select_polygon(small, {.points = points, .contours = counts, .rule = rule});
                });
                test::check(selection::read(ctx, small)[0] == 128, "islands beside crowded rows");
            }
        }
        // More pieces cross each row than workgroup memory holds: 700 thin overlapping
        // columns of both orientations on a canvas wider than several workgroups.
        const int width = 1100, height = 9;
        auto mask = ctx.create_mask({width, height});
        std::vector<Point> points;
        std::vector<std::uint32_t> counts;
        std::vector<std::array<double, 5>> boxes;
        for (int i = 0; i < 700; ++i) {
            const float x = 0.3f + 1.53125f * float(i);
            add_box(points, counts, boxes, x, 0.25f + 0.125f * float(i % 5),
                    x + 0.40625f + 0.5f * float(i % 3), 8.75f - 0.25f * float(i % 7), i % 2 == 0);
        }
        add_box(points, counts, boxes, 5.5f, 3.25f, 1050.75f, 5.5f, false);
        for (const auto rule : {FillRule::nonzero, FillRule::even_odd}) {
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.select_polygon(mask, {.points = points, .contours = counts, .rule = rule});
            });
            selection::expect(selection::read(ctx, mask), box_coverage(boxes, rule, width, height),
                              1, "crowded rows", width);
        }
    });
    test::run("overly complex shapes are rejected before recording", [] {
        auto ctx = Context::create();
        auto mask = ctx.create_mask({4096, 4096});
        std::vector<Point> scribble;
        for (int i = 0; i < 4000; ++i) {
            scribble.push_back({float(i % 2 ? 4000 : 50), float(i % 3 ? 4090 : 3)});
        }
        auto cmd = ctx.create_commands(2);
        test::error(ErrorCode::capacity, "select_polygon", "points",
                    [&] { cmd.select_polygon(mask, {.points = scribble}); });
        const std::array<Point, 3> huge{Point{0, 0}, Point{2e9f, 5}, Point{3, 7}};
        test::error(ErrorCode::invalid_argument, "select_polygon", "transform",
                    [&] { cmd.select_polygon(mask, {.points = huge}); });
    });
    test::run("selection modes combine exactly with existing coverage", [] {
        auto ctx = Context::create();
        const int width = 23, height = 19;
        auto mask = ctx.create_mask({width, height});
        auto other = ctx.create_mask({width, height});
        const auto existing = selection::random_bytes(width * height, 7);
        const EllipseSelectionOptions shape{
            .origin = {3.3f, 2.1f}, .width = 15.2f, .height = 13.7f};
        ctx.run_and_wait([&](Commands& cmd) { cmd.select_ellipse(mask, shape); });
        const auto incoming = selection::read(ctx, mask);
        for (const auto mode : modes) {
            selection::upload(ctx, mask, existing);
            auto options = shape;
            options.mode = mode;
            ctx.run_and_wait([&](Commands& cmd) { cmd.select_ellipse(mask, options); });
            Bytes expected(existing.size());
            for (std::size_t i = 0; i < expected.size(); ++i) {
                expected[i] = selection::combine(existing[i], incoming[i], mode);
            }
            selection::expect(selection::read(ctx, mask), expected, 0, "shape mode", width);

            const auto source = selection::random_bytes(width * height, 11);
            selection::upload(ctx, other, source);
            selection::upload(ctx, mask, existing);
            ctx.run_and_wait([&](Commands& cmd) { cmd.combine(other, mask, {.mode = mode}); });
            for (std::size_t i = 0; i < expected.size(); ++i) {
                expected[i] = selection::combine(existing[i], source[i], mode);
            }
            selection::expect(selection::read(ctx, mask), expected, 0, "mask combine", width);
        }
    });
    test::run("feathered shapes blur the new shape before combining", [] {
        auto ctx = Context::create();
        const int width = 31, height = 26;
        auto mask = ctx.create_mask({width, height});
        const auto required = feather_requirements({width, height}, {.radius = 2.5f});

        auto scratch = ctx.create_workspace(required.workspace);
        const std::vector<Point> triangle{{3.5f, 22}, {15.2f, 2.4f}, {28.9f, 23.3f}};
        ctx.run_and_wait([&](Commands& cmd) { cmd.select_polygon(mask, {.points = triangle}); });
        const auto hard = selection::read(ctx, mask);
        const auto soft = feathered(hard, width, height, 2.5);
        const auto existing = selection::random_bytes(width * height, 3);
        for (const auto mode : {SelectionMode::replace, SelectionMode::subtract}) {
            selection::upload(ctx, mask, existing);
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.select_polygon(
                    mask, {.points = triangle, .mode = mode, .feather = 2.5f, .workspace = scratch});
            });
            Bytes expected(soft.size());
            for (std::size_t i = 0; i < expected.size(); ++i) {
                expected[i] = selection::combine(existing[i], soft[i], mode);
            }
            selection::expect(selection::read(ctx, mask), expected, 1, "feathered triangle", width);
        }
    });
    test::run("shape validation happens before recording", [] {
        auto ctx = Context::create();
        auto mask = ctx.create_mask({8, 8});
        auto small = ctx.create_workspace(feather_requirements({4, 4}, {.radius = 1}).workspace);
        auto cmd = ctx.create_commands(4);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        test::error(ErrorCode::invalid_argument, "select_rectangle", "width",
                    [&] { cmd.select_rectangle(mask, {.width = -1, .height = 2}); });
        test::error(ErrorCode::invalid_argument, "select_rectangle", "mode", [&] {
            cmd.select_rectangle(mask, {.width = 1, .height = 1, .mode = SelectionMode(9)});
        });
        test::error(ErrorCode::invalid_argument, "select_ellipse", "transform", [&] {
            cmd.select_ellipse(mask, {.width = 1, .height = 1, .transform = {nan, 0, 0, 1, 0, 0}});
        });
        test::error(ErrorCode::capacity, "select_ellipse", "workspace",
                    [&] { cmd.select_ellipse(mask, {.width = 4, .height = 4, .feather = 1}); });
        test::error(ErrorCode::capacity, "select_ellipse", "workspace", [&] {
            cmd.select_ellipse(mask, {.width = 4, .height = 4, .feather = 1, .workspace = small});
        });
        const std::array<Point, 3> points{Point{0, 0}, Point{nan, 1}, Point{2, 2}};
        test::error(ErrorCode::invalid_argument, "select_polygon", "points",
                    [&] { cmd.select_polygon(mask, {.points = points}); });
        const std::array<Point, 3> valid{Point{0, 0}, Point{4, 1}, Point{2, 5}};
        const std::array<std::uint32_t, 1> wrong{2};
        test::error(ErrorCode::invalid_argument, "select_polygon", "contours",
                    [&] { cmd.select_polygon(mask, {.points = valid, .contours = wrong}); });
        test::error(ErrorCode::invalid_argument, "select_polygon", "rule",
                    [&] { cmd.select_polygon(mask, {.points = valid, .rule = FillRule(2)}); });
        test::error(ErrorCode::invalid_argument, "combine", "destination",
                    [&] { cmd.combine(mask, mask); });
        // Nothing was recorded: the fill below is the only write.
        cmd.fill(mask, {.coverage = 1});
        ctx.submit_and_wait(cmd);
        for (auto byte : selection::read(ctx, mask)) {
            test::check(byte == 255, "rejected shape wrote coverage");
        }
        // An empty polygon replaces the selection with nothing.
        ctx.run_and_wait([&](Commands& c) { c.select_polygon(mask, {}); });
        for (auto byte : selection::read(ctx, mask)) {
            test::check(byte == 0, "empty polygon must deselect");
        }
    });
    return test::finish();
}
