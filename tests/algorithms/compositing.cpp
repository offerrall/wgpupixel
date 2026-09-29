#include "../test.h"
#include <array>
#include <cstring>
#include <limits>

using namespace wgpupixel;

namespace {
std::string case_name;
using RGB = std::array<double, 3>;
using Pixel = std::array<float, 4>;
double lum(RGB c) {
    return .3 * c[0] + .59 * c[1] + .11 * c[2];
}
double sat(RGB c) {
    return *std::max_element(c.begin(), c.end()) - *std::min_element(c.begin(), c.end());
}
RGB set_sat(RGB c, double s) {
    std::array<int, 3> order{0, 1, 2};
    std::sort(order.begin(), order.end(), [&](int a, int b) { return c[a] < c[b]; });
    const auto lo = order[0], mid = order[1], hi = order[2];
    if (c[hi] > c[lo]) {
        c[mid] = (c[mid] - c[lo]) * s / (c[hi] - c[lo]);
        c[hi] = s;
    } else {
        c[mid] = c[hi] = 0;
    }
    c[lo] = 0;
    return c;
}
RGB set_lum(RGB c, double l) {
    const double delta = l - lum(c);
    for (auto& v : c) {
        v += delta;
    }
    const double n = *std::min_element(c.begin(), c.end());
    const double x = *std::max_element(c.begin(), c.end());
    if (n < 0) {
        for (auto& v : c) {
            v = l + (v - l) * l / (l - n);
        }
    }
    if (x > 1) {
        for (auto& v : c) {
            v = l + (v - l) * (1 - l) / (x - l);
        }
    }
    return c;
}
double dodge(double b, double s) {
    s = std::clamp(s, 0., 1.);
    if (s == 1) {
        return b == 0 ? 0 : std::max(1., b);
    }
    return std::clamp(b / (1 - s), b, std::max(1., b));
}
double burn(double b, double s) {
    s = std::clamp(s, 0., 1.);
    if (s == 0) {
        return b == 1 ? 1 : std::min(0., b);
    }
    return std::clamp(1 - (1 - b) / s, std::min(0., b), b);
}
double vivid(double b, double s) {
    return s <= .5 ? burn(b, 2 * s) : dodge(b, 2 * s - 1);
}
RGB blend(RGB b, RGB s, int mode) {
    if (mode >= 23 && mode <= 26) {
        const double low = std::min({0., b[0], b[1], b[2], s[0], s[1], s[2]});
        const double high = std::max({1., b[0], b[1], b[2], s[0], s[1], s[2]});
        for (int c = 0; c < 3; ++c) {
            b[c] = (b[c] - low) / (high - low);
            s[c] = (s[c] - low) / (high - low);
        }
        RGB result;
        switch (mode) {
        case 23:
            result = set_lum(set_sat(s, sat(b)), lum(b));
            break;
        case 24:
            result = set_lum(set_sat(b, sat(s)), lum(b));
            break;
        case 25:
            result = set_lum(s, lum(b));
            break;
        default:
            result = set_lum(b, lum(s));
            break;
        }
        for (auto& v : result) {
            v = v * (high - low) + low;
        }
        return result;
    }
    switch (mode) {
    case 21:
        return s[0] + s[1] + s[2] < b[0] + b[1] + b[2] ? s : b;
    case 22:
        return s[0] + s[1] + s[2] > b[0] + b[1] + b[2] ? s : b;
    }
    RGB result{};
    for (int c = 0; c < 3; ++c) {
        const double x = b[c], y = s[c];
        double v = y;
        switch (mode) {
        case 1:
            v = x * y;
            break;
        case 2:
            v = x + y - x * y;
            break;
        case 3:
            v = x <= .5 ? 2 * x * y : 1 - 2 * (1 - x) * (1 - y);
            break;
        case 4:
            v = std::min(x, y);
            break;
        case 5:
            v = std::max(x, y);
            break;
        case 6:
            v = std::abs(x - y);
            break;
        case 7:
            v = x + y - 2 * x * y;
            break;
        case 8:
            v = x + y;
            break;
        case 9: {
            const double d = x <= .25 ? ((16 * x - 12) * x + 4) * x : std::sqrt(x);
            v = y <= .5 ? x - (1 - 2 * y) * x * (1 - x) : x + (2 * y - 1) * (d - x);
            break;
        }
        case 10:
            v = y <= .5 ? 2 * x * y : 1 - 2 * (1 - x) * (1 - y);
            break;
        case 11:
            v = dodge(x, y);
            break;
        case 12:
            v = burn(x, y);
            break;
        case 13:
            v = std::max(std::min(0., x), x + y - 1);
            break;
        case 14:
            v = std::min(std::max(1., x), x + y);
            break;
        case 15:
            v = std::clamp(x + 2 * y - 1, std::min(0., x), std::max(1., x));
            break;
        case 16:
            v = vivid(x, y);
            break;
        case 17:
            v = y <= .5 ? std::min(x, 2 * y) : std::max(x, 2 * y - 1);
            break;
        case 18:
            v = float(x) + float(y) >= 1.f ? 1 : 0;
            break;
        case 19:
            v = std::max(std::min(0., x), x - y);
            break;
        case 20:
            v = y <= 0 ? std::max(1., x) : std::min(std::max(1., x), x / y);
            break;
        }
        result[c] = v;
    }
    return result;
}
float random_value(std::uint32_t x, std::uint32_t y, std::uint32_t seed) {
    auto h = seed ^ (x * 0x9e3779b9u) ^ (y * 0x85ebca6bu);
    h = (h ^ (h >> 16)) * 0x7feb352du;
    h = (h ^ (h >> 15)) * 0x846ca68bu;
    h ^= h >> 16;
    return float(h >> 8) / 16777216.f;
}
double range(double v, BlendIfRange r) {
    if (v < r.black || v > r.white) {
        return 0;
    }
    const double a = r.black_split == r.black
                         ? 1
                         : std::clamp((v - r.black) / (r.black_split - r.black), 0., 1.);
    const double b = r.white == r.white_split
                         ? 1
                         : std::clamp((r.white - v) / (r.white - r.white_split), 0., 1.);
    return a * b;
}
Pixel compose(Pixel s, Pixel b, int mode, float opacity, int x, int y,
              const BlendLayerOptions& options = {}) {
    if (s[3] == 0) {
        return b;
    }
    RGB cs{}, cb{};
    for (int c = 0; c < 3; ++c) {
        cs[c] = double(s[c]) / s[3];
        cb[c] = b[3] == 0 ? 0 : double(b[c]) / b[3];
    }
    if (options.blend_if) {
        opacity *= range(lum(cs), options.blend_if->source) *
                   range(lum(cb), options.blend_if->destination);
    }
    double a = double(s[3]) * opacity;
    if (mode == 27) {
        a = a > random_value(x, y, options.seed.value_or(0)) ? 1 : 0;
    }
    const auto color = blend(cb, cs, mode);
    Pixel out{};
    out[3] = options.preserve_alpha ? b[3] : a + b[3] * (1 - a);
    for (int c = 0; c < 3; ++c) {
        out[c] = options.preserve_alpha
                     ? b[3] * ((1 - a) * cb[c] + a * color[c])
                     : (1 - a) * b[c] + a * (1 - b[3]) * cs[c] + a * b[3] * color[c];
    }
    return out;
}
void upload(Context& ctx, const Image& image, const std::vector<Pixel>& pixels) {
    auto buffer = ctx.create_upload_buffer(image, {.format = TransferFormat::rgba32_float});
    ctx.write(buffer, {reinterpret_cast<const std::uint8_t*>(pixels.data()),
                       pixels.size() * sizeof(Pixel)});
    ctx.run_and_wait([&](Commands& cmd) { cmd.upload(buffer, image); });
    ctx.destroy(buffer);
}
void expect(Context& ctx, const Image& image, const std::vector<Pixel>& expected,
            bool exact_alpha = false) {
    auto buffer = ctx.create_readback_buffer(image, {.format = TransferFormat::rgba32_float});
    ctx.run_and_wait([&](Commands& cmd) { cmd.download(image, buffer); });
    std::vector<Pixel> actual(expected.size());
    ctx.read(buffer,
             {reinterpret_cast<std::uint8_t*>(actual.data()), actual.size() * sizeof(Pixel)});
    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (exact_alpha) {
            test::check(actual[i][3] == expected[i][3], "alpha lock must be exact");
        }
        for (int c = 0; c < 4; ++c) {
            test::check(std::isfinite(actual[i][c]) &&
                            std::abs(actual[i][c] - expected[i][c]) <= 4e-5f,
                        case_name + " pixel " + std::to_string(i) + " channel " +
                            std::to_string(c) + " expected " + std::to_string(expected[i][c]) +
                            " got " + std::to_string(actual[i][c]));
        }
    }
    ctx.destroy(buffer);
}
std::vector<Pixel> fixture(int count, int offset) {
    constexpr std::array values{0.f, .01f, .2f, .25f, .5f, .75f, .99f, 1.f};
    std::vector<Pixel> pixels(count);
    for (int i = 0; i < count; ++i) {
        auto& p = pixels[i];
        p[3] = values[(i / 8 + offset) % 8];
        for (int c = 0; c < 3; ++c) {
            p[c] = values[(i + c * (i / 8 + 1) + offset) % 8] * p[3];
        }
    }
    return pixels;
}
} // namespace

int main() {
    auto ctx = Context::create();
    test::run("all 28 blend modes match float CPU source-over with alpha endpoints", [&] {
        auto source = ctx.create_image({16, 16});
        auto destination = ctx.create_image({16, 16});
        const auto s = fixture(256, 0), b = fixture(256, 3);
        upload(ctx, source, s);
        for (int mode = 0; mode <= 27; ++mode) {
            for (float opacity : {0.f, .37f, 1.f}) {
                case_name = "mode " + std::to_string(mode) + " opacity " + std::to_string(opacity);
                auto expected = b;
                for (int i = 0; i < 256; ++i) {
                    expected[i] = compose(s[i], b[i], mode, opacity, i % 16, i / 16);
                }
                upload(ctx, destination, b);
                ctx.run_and_wait([&](Commands& cmd) {
                    cmd.blend(source, destination,
                              {.opacity = opacity, .mode = BlendMode(mode), .seed = 0});
                });
                expect(ctx, destination, expected);
                upload(ctx, destination, b);
                const std::array sources{source};
                const std::array positions{Position{}};
                const std::array opacities{opacity};
                const std::array modes{BlendMode(mode)};
                const std::array layers{BlendLayerOptions{.seed = 0}};
                ctx.run_and_wait([&](Commands& cmd) {
                    cmd.blend_many(sources, destination,
                                   {.positions = positions,
                                    .opacities = opacities,
                                    .modes = modes,
                                    .layers = layers});
                });
                expect(ctx, destination, expected);
            }
        }
    });
    test::run("non-neutral signed HDR layers match double reference in both paths", [&] {
        auto source = ctx.create_image({17, 13}), destination = ctx.create_image({17, 13});
        constexpr std::array backdrops{-4.f, -.5f, .2f, .75f, 1.f, 2.f, 4.f, 8.f};
        constexpr std::array foregrounds{-1.f, -.25f, .125f, .375f, .625f, .875f, 1.f, 2.f};
        std::vector<Pixel> s(221), b(221);
        for (int i = 0; i < 221; ++i) {
            s[i][3] = i % 3 == 0 ? .125f : i % 3 == 1 ? .5f : 1.f;
            b[i][3] = i % 4 == 0 ? 0.f : i % 4 == 1 ? .25f : 1.f;
            for (int c = 0; c < 3; ++c) {
                s[i][c] = foregrounds[(i / 8 + c) % 8] * s[i][3];
                b[i][c] = backdrops[(i + 3 * c) % 8] * b[i][3];
            }
        }
        upload(ctx, source, s);
        for (int mode = 11; mode <= 26; ++mode) {
            case_name = "HDR mode " + std::to_string(mode);
            const std::array sources{source};
            const std::array positions{Position{}};
            const std::array modes{BlendMode(mode)};
            const std::array opacities{.75f};
            auto expected = b;
            for (int i = 0; i < 221; ++i) {
                expected[i] = compose(s[i], b[i], mode, .75f, i % 17, i / 17);
            }
            for (bool batched : {false, true}) {
                upload(ctx, destination, b);
                ctx.run_and_wait([&](Commands& cmd) {
                    if (batched) {
                        cmd.blend_many(
                            sources, destination,
                            {.positions = positions, .opacities = opacities, .modes = modes});
                    } else {
                        cmd.blend(source, destination, {.opacity = .75f, .mode = BlendMode(mode)});
                    }
                });
                expect(ctx, destination, expected);
            }
        }
    });
    test::run("all modes honor moving A8 masks region alpha lock and Blend If", [&] {
        auto source = ctx.create_image({16, 16});
        auto destination = ctx.create_image({16, 16});
        auto sm = ctx.create_mask({16, 16}), dm = ctx.create_mask({16, 16});
        const auto s = fixture(256, 0), b = fixture(256, 2);
        upload(ctx, source, s);
        auto buffer = ctx.create_upload_buffer(sm);
        std::vector<std::uint8_t> coverage(256);
        for (int i = 0; i < 256; ++i) {
            coverage[i] = std::uint8_t((i % 4) * 85);
        }
        ctx.write(buffer, coverage);
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.upload(buffer, sm);
            cmd.upload(buffer, dm);
        });
        for (int mode = 0; mode <= 27; ++mode) {
            for (int flags = 0; flags < 4; ++flags) {
                case_name = "mode " + std::to_string(mode) + " flags " + std::to_string(flags);
                BlendLayerOptions layer{
                    .source_mask = &sm, .preserve_alpha = (flags & 1) != 0, .seed = 918273};
                if (flags & 2) {
                    layer.blend_if = BlendIfOptions{.source = {.05f, .3f, .7f, .95f},
                                                    .destination = {.1f, .2f, .8f, .9f}};
                }
                auto expected = b;
                for (int y = 2; y < 13; ++y) {
                    for (int x = 3; x < 14; ++x) {
                        const int sx = x + 2, sy = y - 1;
                        const int i = y * 16 + x, si = sy * 16 + sx;
                        auto result =
                            compose(s[si], b[i], mode, .73f * coverage[si] / 255.f, sx, sy, layer);
                        for (int c = 0; c < 4; ++c) {
                            expected[i][c] = b[i][c] + (result[c] - b[i][c]) * coverage[i] / 255.f;
                        }
                    }
                }
                upload(ctx, destination, b);
                ctx.run_and_wait([&](Commands& cmd) {
                    cmd.blend(source, destination,
                              {.position = {-2, 1},
                               .opacity = .73f,
                               .mode = BlendMode(mode),
                               .mask = &dm,
                               .region = Rect{3, 2, 11, 11},
                               .source_mask = &sm,
                               .preserve_alpha = layer.preserve_alpha,
                               .seed = layer.seed,
                               .blend_if = layer.blend_if});
                });
                expect(ctx, destination, expected);
                upload(ctx, destination, b);
                const std::array sources{source};
                const std::array positions{Position{-2, 1}};
                const std::array opacities{.73f};
                const std::array modes{BlendMode(mode)};
                const std::array layers{layer};
                ctx.run_and_wait([&](Commands& cmd) {
                    cmd.blend_many(sources, destination,
                                   {.positions = positions,
                                    .opacities = opacities,
                                    .modes = modes,
                                    .mask = &dm,
                                    .region = Rect{3, 2, 11, 11},
                                    .layers = layers});
                });
                expect(ctx, destination, expected);
            }
        }
    });
    test::run("five layers agree across batch boundaries for all modes", [&] {
        auto source = ctx.create_image({16, 16});
        auto destination = ctx.create_image({16, 16});
        const auto s = fixture(256, 0), b = fixture(256, 3);
        upload(ctx, source, s);
        const std::array sources{source, source, source, source, source};
        const std::array positions{Position{}, Position{}, Position{}, Position{}, Position{}};
        const std::array opacities{.7f, .4f, .3f, .2f, .8f};
        auto mask = ctx.create_mask({16, 16});
        ctx.run_and_wait([&](Commands& cmd) { cmd.fill(mask, {.coverage = .6f}); });
        for (int mode = 0; mode <= 27; ++mode) {
            case_name = "five layers mode " + std::to_string(mode);
            const std::array modes{BlendMode(mode), BlendMode(mode), BlendMode(mode),
                                   BlendMode(mode), BlendMode(mode)};
            for (bool advanced : {false, true}) {
                std::array<BlendLayerOptions, 5> layers{};
                for (auto& layer : layers) {
                    layer.seed = 0;
                }
                if (advanced) {
                    for (auto& layer : layers) {
                        layer.preserve_alpha = true;
                    }
                }
                auto expected = b;
                for (int y = 1; y < 15; ++y) {
                    for (int x = 1; x < 15; ++x) {
                        const int i = y * 16 + x;
                        for (int j = 0; j < 5; ++j) {
                            const auto result =
                                compose(s[i], expected[i], mode, opacities[j], x, y, layers[j]);
                            for (int c = 0; c < 4; ++c) {
                                expected[i][c] += (result[c] - expected[i][c]) * .6f;
                            }
                        }
                    }
                }
                upload(ctx, destination, b);
                ctx.run_and_wait([&](Commands& cmd) {
                    cmd.blend_many(sources, destination,
                                   {.positions = positions,
                                    .opacities = opacities,
                                    .modes = modes,
                                    .mask = &mask,
                                    .region = Rect{1, 1, 14, 14},
                                    .layers = layers});
                });
                expect(ctx, destination, expected, advanced);
            }
        }
    });
    test::run("packed source mask tail and extreme offsets", [&] {
        auto source = ctx.create_image({3, 1}), destination = ctx.create_image({3, 1});
        auto mask = ctx.create_mask({3, 1});
        auto buffer = ctx.create_upload_buffer(mask);
        const std::array<std::uint8_t, 3> bytes{0, 128, 255};
        ctx.write(buffer, bytes);
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.upload(buffer, mask);
            cmd.fill(source, {.color = {1, 0, 0, 1}});
            cmd.fill(destination, {.color = {0, 0, 0, 0}});
            cmd.blend(source, destination, {.source_mask = &mask});
            cmd.blend(
                source, destination,
                {.position = {std::numeric_limits<std::int32_t>::min(), 0}, .source_mask = &mask});
            cmd.blend(
                source, destination,
                {.position = {std::numeric_limits<std::int32_t>::max(), 0}, .source_mask = &mask});
        });
        expect(ctx, destination, {{0, 0, 0, 0}, {128.f / 255, 0, 0, 128.f / 255}, {1, 0, 0, 1}});
    });
    test::run("advanced blend validation is atomic and retains source masks", [&] {
        auto source = ctx.create_image({2, 2}), destination = ctx.create_image({2, 2});
        auto wrong = ctx.create_mask({1, 1}), mask = ctx.create_mask({2, 2});
        auto cmd = ctx.create_commands(2);
        test::error(ErrorCode::invalid_argument, "blend", "source_mask",
                    [&] { cmd.blend(source, destination, {.source_mask = &wrong}); });
        test::error(ErrorCode::invalid_argument, "blend", "mode",
                    [&] { cmd.blend(source, destination, {.mode = BlendMode(28)}); });
        for (auto r : {BlendIfRange{.4f, .3f, .7f, 1},
                       BlendIfRange{0, 0, 1, std::numeric_limits<float>::infinity()}}) {
            test::error(ErrorCode::invalid_argument, "blend", "blend_if", [&] {
                cmd.blend(source, destination, {.blend_if = BlendIfOptions{r, {}}});
            });
        }
        const std::array sources{source, source};
        const std::array positions{Position{}, Position{}};
        const std::array layers{BlendLayerOptions{.source_mask = &mask},
                                BlendLayerOptions{.source_mask = &wrong}};
        test::error(ErrorCode::invalid_argument, "blend_many", "source_mask", [&] {
            cmd.blend_many(sources, destination, {.positions = positions, .layers = layers});
        });
        ctx.destroy(mask);
        mask = ctx.create_mask({2, 2});
        cmd.blend(source, destination, {.source_mask = &mask});
        test::error(ErrorCode::resource_busy, "destroy", "resource", [&] { ctx.destroy(mask); });
        ctx.submit_and_wait(cmd);
        ctx.destroy(mask);
    });
    return test::finish();
}
