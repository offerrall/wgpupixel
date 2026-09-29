#include "selection_support.h"
#include <deque>
#include <limits>

using namespace wgpupixel;
using selection::Bytes;

namespace {
struct Picture {
    int width, height;
    Bytes rgba; // Straight sRGB RGBA8; alpha is 0 or 255 so GPU round trips are exact.
    std::array<int, 4> at(int x, int y) const {
        const auto* p = &rgba[(std::size_t(y) * width + x) * 4];
        if (p[3] == 0) {
            return {0, 0, 0, 0};
        }
        return {p[0], p[1], p[2], p[3]};
    }
    void set(int x, int y, std::array<std::uint8_t, 4> color) {
        std::copy(color.begin(), color.end(), rgba.begin() + (std::size_t(y) * width + x) * 4);
    }
};

std::array<int, 4> reference(const Picture& picture, Position seed, int radius) {
    std::array<long, 4> sum{};
    long count = 0;
    for (int y = std::max(0, seed.y - radius); y <= std::min(picture.height - 1, seed.y + radius);
         ++y) {
        for (int x = std::max(0, seed.x - radius);
             x <= std::min(picture.width - 1, seed.x + radius); ++x) {
            const auto color = picture.at(x, y);
            for (int c = 0; c < 4; ++c) {
                sum[c] += color[c];
            }
            ++count;
        }
    }
    std::array<int, 4> mean{};
    for (int c = 0; c < 4; ++c) {
        mean[c] = int((2 * sum[c] + count) / (2 * count));
    }
    return mean;
}

// Breadth-first flood fill (or every match) as the CPU reference.
std::vector<bool> flood(const Picture& picture, const MagicWandOptions& options) {
    const auto target = reference(picture, options.seed, int(options.sample_radius));
    const auto matches = [&](int x, int y) {
        const auto color = picture.at(x, y);
        int largest = 0;
        for (int c = 0; c < 4; ++c) {
            largest = std::max(largest, std::abs(color[c] - target[c]));
        }
        return largest <= options.tolerance * 255.0f;
    };
    const int width = picture.width, height = picture.height;
    std::vector<bool> selected(std::size_t(width) * height);
    if (!options.contiguous) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                selected[y * width + x] = matches(x, y);
            }
        }
        return selected;
    }
    if (!matches(options.seed.x, options.seed.y)) {
        return selected;
    }
    std::deque<std::array<int, 2>> queue{{options.seed.x, options.seed.y}};
    selected[options.seed.y * width + options.seed.x] = true;
    while (!queue.empty()) {
        const auto [x, y] = queue.front();
        queue.pop_front();
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if ((dx == 0 && dy == 0) || (!options.diagonal && dx != 0 && dy != 0)) {
                    continue;
                }
                const int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= width || ny >= height || selected[ny * width + nx] ||
                    !matches(nx, ny)) {
                    continue;
                }
                selected[ny * width + nx] = true;
                queue.push_back({nx, ny});
            }
        }
    }
    return selected;
}

Bytes coverage(const std::vector<bool>& selected, int width, int height, bool anti_alias) {
    Bytes result(selected.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double center = selected[y * width + x];
            double value = center;
            if (anti_alias) {
                double tent = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int sx = std::clamp(x + dx, 0, width - 1);
                        const int sy = std::clamp(y + dy, 0, height - 1);
                        tent += (2 - std::abs(dx)) * (2 - std::abs(dy)) * selected[sy * width + sx];
                    }
                }
                value = 0.5 * center + 0.5 * tent / 16;
            }
            result[y * width + x] = selection::quantize(value);
        }
    }
    return result;
}

Picture spiral(int width, int height) {
    Picture picture{width, height, Bytes(std::size_t(width) * height * 4)};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            picture.set(x, y, {30, 60, 90, 255});
        }
    }
    const auto carve = [&](int x0, int y0, int x1, int y1) {
        for (int y = std::min(y0, y1); y <= std::max(y0, y1); ++y) {
            for (int x = std::min(x0, x1); x <= std::max(x0, x1); ++x) {
                picture.set(x, y, {200, 180, 40, 255});
            }
        }
    };
    for (int k = 0;; ++k) {
        const int left = 1 + 2 * k, top = 1 + 2 * k;
        const int right = width - 2 - 2 * k, bottom = height - 2 - 2 * k;
        if (left > right || top > bottom) {
            break;
        }
        carve(k == 0 ? left : left - 2, top, right, top);
        carve(right, top, right, bottom);
        carve(right, bottom, left, bottom);
        if (top + 2 <= bottom) {
            carve(left, bottom, left, top + 2);
        }
    }
    return picture;
}

Picture noise(int width, int height, std::uint32_t seed, int palette) {
    Picture picture{width, height, Bytes(std::size_t(width) * height * 4)};
    std::mt19937 random(seed);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int shade = int(random() % palette);
            if (shade == 0 && palette > 2) {
                picture.set(x, y, {0, 0, 0, 0}); // Transparent.
                continue;
            }
            picture.set(x, y,
                        {std::uint8_t(40 + 25 * shade + random() % 9),
                         std::uint8_t(100 + random() % 9), std::uint8_t(160 - 11 * shade), 255});
        }
    }
    return picture;
}

void check_wand(Context& ctx, const Picture& picture, const MagicWandOptions& options,
                std::string_view what) {
    auto image = ctx.create_image({picture.width, picture.height});
    auto mask = ctx.create_mask({picture.width, picture.height});
    auto with = options;
    with.workspace = ctx.create_workspace(select_magic_wand_requirements(image.size(), options).workspace);
    selection::upload(ctx, image, picture.rgba);
    ctx.run_and_wait([&](Commands& cmd) { cmd.select_magic_wand(image, mask, with); });
    selection::expect(
        selection::read(ctx, mask),
        coverage(flood(picture, options), picture.width, picture.height, options.anti_alias), 0,
        what, picture.width);
    ctx.destroy(with.workspace);
    ctx.destroy(mask);
    ctx.destroy(image);
}
} // namespace

int main() {
    test::run("magic wand follows a one-pixel spiral exactly", [] {
        auto ctx = Context::create();
        for (const auto size : {std::array{203, 157}, std::array{64, 64}, std::array{517, 263}}) {
            const auto picture = spiral(size[0], size[1]);
            for (const bool diagonal : {false, true}) {
                check_wand(ctx, picture,
                           {.seed = {1, 1},
                            .tolerance = 0.0f / 255.0f,
                            .anti_alias = false,
                            .diagonal = diagonal},
                           "spiral corridor");
                check_wand(ctx, picture,
                           {.seed = {0, 0}, .tolerance = 10.0f / 255.0f, .diagonal = diagonal},
                           "spiral walls");
            }
        }
    });
    test::run("magic wand connectivity is exact on random mazes", [] {
        auto ctx = Context::create();
        for (const auto size : {std::array{257, 131}, std::array{1, 97}, std::array{97, 1},
                                std::array{16, 16}, std::array{1024, 768}}) {
            const auto picture = noise(size[0], size[1], std::uint32_t(size[0] * 31 + size[1]), 2);
            for (const bool diagonal : {false, true}) {
                for (const auto seed : {Position{0, 0}, Position{size[0] / 2, size[1] / 2},
                                        Position{size[0] - 1, size[1] - 1}}) {
                    check_wand(ctx, picture,
                               {.seed = seed,
                                .tolerance = 20.0f / 255.0f,
                                .anti_alias = false,
                                .diagonal = diagonal},
                               "random maze");
                }
            }
        }
    });
    test::run("tolerance, transparency, sampling and anti-aliasing", [] {
        auto ctx = Context::create();
        const auto picture = noise(83, 61, 12, 5);
        for (const float tolerance : {0.0f, 12.0f, 26.0f, 255.0f}) {
            for (const bool contiguous : {true, false}) {
                for (const std::int64_t sample : {0, 2}) {
                    check_wand(ctx, picture,
                               {.seed = {1, 2},
                                .tolerance = tolerance / 255.0f,
                                .sample_radius = sample,
                                .contiguous = contiguous},
                               "wand options");
                }
            }
        }
        // Clicking a transparent pixel selects the connected transparent area.
        for (int y = 0; y < picture.height; ++y) {
            for (int x = 0; x < picture.width; ++x) {
                if (picture.at(x, y)[3] == 0) {
                    check_wand(ctx, picture, {.seed = {x, y}, .tolerance = 0.0f / 255.0f},
                               "transparent");
                    return;
                }
            }
        }
    });
    test::run("magic wand combines with the existing selection", [] {
        auto ctx = Context::create();
        const auto picture = noise(40, 30, 3, 3);
        auto image = ctx.create_image({40, 30});
        auto mask = ctx.create_mask({40, 30});
        auto scratch = ctx.create_workspace(select_magic_wand_requirements({40, 30}, {}).workspace);
        selection::upload(ctx, image, picture.rgba);
        const auto existing = selection::random_bytes(1200, 8);
        const MagicWandOptions base{.seed = {5, 5}, .tolerance = 30.0f / 255.0f};
        const auto incoming = coverage(flood(picture, base), 40, 30, true);
        for (const auto mode : {SelectionMode::add, SelectionMode::subtract,
                                SelectionMode::intersect, SelectionMode::difference}) {
            selection::upload(ctx, mask, existing);
            auto options = base;
            options.mode = mode;
            options.workspace = scratch;
            ctx.run_and_wait([&](Commands& cmd) { cmd.select_magic_wand(image, mask, options); });
            Bytes expected(existing.size());
            for (std::size_t i = 0; i < expected.size(); ++i) {
                expected[i] = selection::combine(existing[i], incoming[i], mode);
            }
            selection::expect(selection::read(ctx, mask), expected, 0, "wand mode", 40);
        }
        auto cmd = ctx.create_commands(8);
        test::error(ErrorCode::invalid_argument, "select_magic_wand", "seed",
                    [&] { cmd.select_magic_wand(image, mask, {.seed = {40, 0}, .workspace = scratch}); });
        test::error(ErrorCode::invalid_argument, "select_magic_wand", "tolerance", [&] {
            cmd.select_magic_wand(image, mask, {.tolerance = 256.0f / 255.0f, .workspace = scratch});
        });
        test::error(ErrorCode::invalid_argument, "select_magic_wand", "sample_radius",
                    [&] { cmd.select_magic_wand(image, mask, {.sample_radius = 51, .workspace = scratch}); });
        test::error(ErrorCode::capacity, "select_magic_wand", "workspace",
                    [&] { cmd.select_magic_wand(image, mask, {}); });
        auto other = ctx.create_mask({39, 30});
        test::error(ErrorCode::invalid_argument, "select_magic_wand", "destination",
                    [&] { cmd.select_magic_wand(image, other, {.workspace = scratch}); });
        test::error(ErrorCode::invalid_argument, "select_magic_wand_requirements", "tolerance",
                    [] { (void)select_magic_wand_requirements({4, 4}, {.tolerance = -1}); });
        // A 4-byte label per pixel plus four reference words: 5 * 3 * 4 + 16 = 76 bytes,
        // packed in rows of 3 bytes (26 rows) and rounded to 4-byte words.
        test::check(select_magic_wand_requirements({3, 5}, {}).workspace.bytes() == 80,
                    "labels plus reference words");
    });
    test::run("HDR and negative colors are compared without clipping", [] {
        auto ctx = Context::create();
        // Linear premultiplied pixels: white, 4x white, 1.05x white, a negative red and
        // a half-transparent HDR pixel. Extended sRGB (odd symmetry) keeps them apart.
        auto image = ctx.create_image({5, 1});
        const std::array<float, 20> pixels{1,     1, 1,     1,    4,    4, 4, 1, 1.05f, 1.05f,
                                           1.05f, 1, -0.2f, 0.5f, 0.5f, 1, 1, 1, 1,     0.5f};
        test::paint(ctx, image, pixels);
        const auto encode = [](double linear) {
            const double m = std::abs(linear);
            const double e = m <= 0.0031308 ? 12.92 * m : 1.055 * std::pow(m, 1 / 2.4) - 0.055;
            return std::copysign(e * 255, linear);
        };
        std::vector<std::array<double, 4>> straight;
        for (int i = 0; i < 5; ++i) {
            const double a = pixels[4 * i + 3];
            straight.push_back({encode(pixels[4 * i] / a), encode(pixels[4 * i + 1] / a),
                                encode(pixels[4 * i + 2] / a), a * 255});
        }
        auto mask = ctx.create_mask({5, 1});
        auto scratch = ctx.create_workspace(select_magic_wand_requirements({5, 1}, {}).workspace);
        for (const float tolerance : {3.0f, 32.0f}) {
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.select_magic_wand(image, mask, {.seed = {0, 0},
                                       .tolerance = tolerance / 255.0f,
                                       .contiguous = false,
                                       .anti_alias = false, .workspace = scratch});
            });
            const auto actual = selection::read(ctx, mask);
            for (int i = 0; i < 5; ++i) {
                double largest = 0;
                for (int c = 0; c < 4; ++c) {
                    largest = std::max(
                        largest, std::abs(std::round(straight[i][c]) - std::round(straight[0][c])));
                }
                test::check(actual[i] == (largest <= tolerance ? 255 : 0), "extended wand match");
            }
        }
        // 1.05 white is 5 steps above 255 and must not be clipped onto white.
        test::check(std::round(straight[2][0]) - std::round(straight[0][0]) > 3, "fixture");
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.select_color_range(
                image, mask,
                {.color = {1, 1, 1, 1}, .fuzziness = 40.0f / (255.0f * std::sqrt(3.0f))});
        });
        const auto actual = selection::read(ctx, mask);
        for (int i = 0; i < 5; ++i) {
            const double d =
                std::hypot(straight[i][0] - 255.0, straight[i][1] - 255.0, straight[i][2] - 255.0);
            const double expected = std::clamp(1 - d / 40, 0.0, 1.0) * straight[i][3] / 255;
            test::check(std::abs(actual[i] - selection::quantize(expected)) <= 1,
                        "extended color range " + std::to_string(i));
        }
    });
    test::run("color range falls off with sRGB distance and alpha", [] {
        auto ctx = Context::create();
        const int width = 37, height = 21;
        auto image = ctx.create_image({width, height});
        auto mask = ctx.create_mask({width, height});
        auto rgba = selection::random_bytes(std::size_t(width) * height * 4, 21);
        for (std::size_t i = 0; i < rgba.size(); i += 4) {
            rgba[i + 3] = i % 12 == 0 ? 0 : i % 20 == 4 ? 128 : 255;
            if (i % 3 == 0) {
                rgba[i] = 200, rgba[i + 1] = 90, rgba[i + 2] = 50; // Near the target.
                rgba[i + 2] += std::uint8_t(i % 40);
            }
        }
        selection::upload(ctx, image, rgba);
        const auto linear = [](double byte) {
            const double s = byte / 255;
            return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
        };
        const double alpha = 0.5;
        const Color target{float(linear(200) * alpha), float(linear(90) * alpha),
                           float(linear(60) * alpha), float(alpha)};
        const auto existing = selection::random_bytes(std::size_t(width) * height, 2);
        for (const float fuzziness : {0.0f, 25.0f, 120.0f}) {
            for (const auto mode : {SelectionMode::replace, SelectionMode::add}) {
                selection::upload(ctx, mask, existing);
                ctx.run_and_wait([&](Commands& cmd) {
                    cmd.select_color_range(image, mask,
                                           {.color = target,
                                            .fuzziness = fuzziness / (255.0f * std::sqrt(3.0f)),
                                            .mode = mode});
                });
                Bytes expected(existing.size());
                for (std::size_t i = 0; i < expected.size(); ++i) {
                    const auto* p = &rgba[i * 4];
                    const double d =
                        p[3] ? std::hypot(p[0] - 200.0, p[1] - 90.0, p[2] - 60.0) : 255;
                    const double value = std::clamp(1 - d / std::max(fuzziness, 0.5f), 0.0, 1.0);
                    expected[i] = selection::combine(existing[i],
                                                     selection::quantize(value * p[3] / 255), mode);
                }
                selection::expect(selection::read(ctx, mask), expected, 1, "color range", width);
            }
        }
        auto cmd = ctx.create_commands(2);
        test::error(ErrorCode::invalid_argument, "select_color_range", "color",
                    [&] { cmd.select_color_range(image, mask, {.color = {0, 0, 0, 0}}); });
        test::error(ErrorCode::invalid_argument, "select_color_range", "fuzziness", [&] {
            cmd.select_color_range(image, mask, {.color = {0, 0, 0, 1}, .fuzziness = 1.01f});
        });
        test::error(ErrorCode::invalid_argument, "select_color_range", "fuzziness", [&] {
            cmd.select_color_range(image, mask, {.color = {0, 0, 0, 1}, .fuzziness = -1});
        });
    });
    return test::finish();
}
