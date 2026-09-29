#include "painting_reference.h"
#include <limits>

using namespace wgpupixel;
using reference::Canvas;
using reference::Pixel;

namespace {
Pixel key(const Pixel& p) {
    if (p[3] <= 0) {
        return {};
    }
    const double alpha = std::min(p[3], 1.0);
    return {reference::srgb_encode(p[0] / p[3]) * alpha,
            reference::srgb_encode(p[1] / p[3]) * alpha,
            reference::srgb_encode(p[2] / p[3]) * alpha, alpha};
}

Canvas bucket(const Canvas& before, const PaintBucketOptions& o,
              const reference::Selection& selection) {
    auto result = before;
    const auto seed = key(before.at(o.seed.x, o.seed.y));
    for (int y = 0; y < before.height; ++y) {
        for (int x = 0; x < before.width; ++x) {
            const auto k = key(before.at(x, y));
            double distance = 0;
            for (int c = 0; c < 4; ++c) {
                distance = std::max(distance, std::abs(k[c] - seed[c]));
            }
            double coverage = distance <= o.tolerance ? 1 : 0;
            if (o.softness > 0) {
                const double t =
                    std::clamp((distance - o.tolerance) / double(o.softness), 0.0, 1.0);
                coverage = 1 - t * t * (3 - 2 * t);
            }
            if (coverage <= 0) {
                continue;
            }
            const auto filled = reference::blend({o.color.r, o.color.g, o.color.b, o.color.a},
                                                 before.at(x, y), o.opacity * coverage, o.mode);
            result.at(x, y) =
                reference::mix(before.at(x, y), filled, selection.at(x, y, before.width));
        }
    }
    return result;
}

// Few distinct colors, so tolerance decides.
Canvas palette(int width, int height) {
    const std::array<Pixel, 5> colors{Pixel{0.2, 0.1, 0.05, 1}, Pixel{0.21, 0.1, 0.05, 1},
                                      Pixel{0.1, 0.4, 0.2, 0.5}, Pixel{}, Pixel{0, 0, 0.002, 0.01}};
    Canvas canvas{width, height, {}};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            canvas.pixels.push_back(colors[std::size_t(x / 3 + y / 2 * 2) % colors.size()]);
        }
    }
    return canvas;
}
} // namespace

int main() {
    test::run("paint bucket fills every pixel within tolerance of the seed", [] {
        auto ctx = Context::create();
        constexpr int width = 19, height = 13;
        auto image = ctx.create_image({width, height});
        auto mask = ctx.create_mask({width, height});
        const auto mask_bytes = reference::ramp_mask(width, height);
        reference::upload(ctx, mask, mask_bytes);
        const auto background = palette(width, height);
        for (const Position seed : {Position{0, 0}, Position{7, 1}, Position{9, 0}}) {
            for (float tolerance : {0.0f, 0.02f, 32.0f / 255}) {
                for (float softness : {0.0f, 0.1f}) {
                    for (int selection = 0; selection < 2; ++selection) {
                        const PaintBucketOptions options{
                            .seed = seed,
                            .tolerance = tolerance,
                            .softness = softness,
                            .color = {0.4f, 0.3f, 0.02f, 0.9f},
                            .mode = selection ? BlendMode::multiply : BlendMode::normal,
                            .opacity = 0.8f,
                            .mask = selection ? &mask : nullptr,
                            .region =
                                selection ? std::optional<Rect>{Rect{2, 1, 9, 10}} : std::nullopt};
                        reference::upload(ctx, image, background);
                        ctx.run_and_wait([&](Commands& cmd) { cmd.paint_bucket(image, options); });
                        const reference::Selection chosen{
                            selection ? std::span<const std::uint8_t>(mask_bytes)
                                      : std::span<const std::uint8_t>{},
                            options.region};
                        reference::expect(reference::download(ctx, image),
                                          bucket(background, options, chosen), 1e-5);
                    }
                }
            }
        }
    });

    test::run("paint bucket validates its options", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({4, 4});
        auto cmd = ctx.create_commands(2);
        const auto expect = [&](std::string_view parameter, PaintBucketOptions options) {
            test::error(ErrorCode::invalid_argument, "paint_bucket", parameter,
                        [&] { cmd.paint_bucket(image, options); });
        };
        expect("seed", {.seed = {4, 0}});
        expect("seed", {.seed = {0, -1}});
        expect("tolerance", {.tolerance = -1});
        expect("softness", {.softness = std::numeric_limits<float>::infinity()});
        expect("color", {.color = {0, 0, 0, -1}});
        expect("opacity", {.opacity = 2});
        auto one = ctx.create_commands(1);
        ctx.set_memory_limit(ctx.memory().total);
        test::error(ErrorCode::capacity, "paint_bucket", "memory_limit",
                    [&] { one.paint_bucket(image, {}); });
        ctx.set_memory_limit(0);
        cmd.paint_bucket(image, {.color = {1, 0, 0, 1}});
        ctx.submit_and_wait(cmd);
    });

    test::run("pattern fill tiles through its transform with masks and regions", [] {
        auto ctx = Context::create();
        constexpr int width = 26, height = 21;
        auto image = ctx.create_image({width, height});
        auto pattern = ctx.create_image({4, 7});
        auto mask = ctx.create_mask({width, height});
        const auto mask_bytes = reference::ramp_mask(width, height);
        reference::upload(ctx, mask, mask_bytes);
        const auto tile = reference::pattern(4, 7, 41);
        reference::upload(ctx, pattern, tile);
        const auto background = reference::pattern(width, height, 42);
        const auto texel = [&](int x, int y) {
            return tile.at(((x % 4) + 4) % 4, ((y % 7) + 7) % 7);
        };
        for (const auto& transform :
             {Affine::identity(), Affine::translate(-3, 5),
              Affine::translate(6, 2) * Affine::rotate(25) * Affine::scale(2, 1.5f)}) {
            for (int selection = 0; selection < 2; ++selection) {
                const PatternFillOptions options{
                    .transform = transform,
                    .mode = selection ? BlendMode::screen : BlendMode::normal,
                    .opacity = selection ? 0.7f : 1.0f,
                    .mask = selection ? &mask : nullptr,
                    .region = selection ? std::optional<Rect>{Rect{-2, 3, 20, 30}} : std::nullopt};
                reference::upload(ctx, image, background);
                ctx.run_and_wait([&](Commands& cmd) { cmd.pattern_fill(pattern, image, options); });
                const auto inverse_transform = *inverse(transform);
                std::vector<double> full(std::size_t(width) * height, 1);
                const auto expected = reference::apply(
                    background, full,
                    {selection ? std::span<const std::uint8_t>(mask_bytes)
                               : std::span<const std::uint8_t>{},
                     options.region},
                    [&](const Pixel& p, int x, int y, double) {
                        const auto q = inverse_transform.map({x + 0.5f, y + 0.5f});
                        const double qx = q.x - 0.5, qy = q.y - 0.5;
                        const double ox = std::floor(qx), oy = std::floor(qy);
                        const int px = int(ox), py = int(oy);
                        const auto sample = reference::mix(
                            reference::mix(texel(px, py), texel(px + 1, py), qx - ox),
                            reference::mix(texel(px, py + 1), texel(px + 1, py + 1), qx - ox),
                            qy - oy);
                        return reference::blend(sample, p, options.opacity, options.mode);
                    });
                reference::expect(reference::download(ctx, image), expected, 3e-5);
            }
        }
        auto cmd = ctx.create_commands(1);
        test::error(ErrorCode::invalid_argument, "pattern_fill", "pattern",
                    [&] { cmd.pattern_fill(image, image, {}); });
        test::error(ErrorCode::invalid_argument, "pattern_fill", "transform",
                    [&] { cmd.pattern_fill(pattern, image, {.transform = {1, 2, 2, 4, 0, 0}}); });
    });
    return test::finish();
}
