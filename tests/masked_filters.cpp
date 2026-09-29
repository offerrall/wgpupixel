#include "test.h"
#include <array>
#include <functional>

using namespace wgpupixel;
namespace {
Mask mask_from(Context& ctx, int width, int height, std::span<const std::uint8_t> values) {
    auto mask = ctx.create_mask({width, height});
    auto upload = ctx.create_upload_buffer(mask);
    ctx.write(upload, values);
    auto cmd = ctx.create_commands(1);
    cmd.upload(upload, mask);
    ctx.submit_and_wait(cmd);
    ctx.destroy(upload);
    return mask;
}
void expect(Context& ctx, const Image& image, std::span<const float> pixels, int tolerance = 2) {
    const auto actual = test::read(ctx, image), encoded = test::encode(pixels);
    test::check(actual.size() == encoded.size(), "masked result size differs");
    for (std::size_t i = 0; i < actual.size(); ++i) {
        test::check(std::abs(int(actual[i]) - int(encoded[i])) <= tolerance,
                    "masked component " + std::to_string(i) + " differs");
    }
}
std::vector<float> mix(std::span<const float> original, std::span<const float> full,
                       std::span<const std::uint8_t> mask) {
    std::vector<float> result(original.size());
    for (std::size_t i = 0; i < result.size(); ++i) {
        const double a = double(mask[i / 4]) / 255;
        result[i] = float(original[i] * (1 - a) + full[i] * a);
    }
    return result;
}
void adjust(std::vector<float>& pixels, float amount) {
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        for (int c = 0; c < 3; ++c) {
            pixels[i + c] += amount * pixels[i + 3];
        }
    }
}
std::vector<float> blur(std::span<const float> pixels, int width, int height, int radius,
                        double sigma) {
    std::vector<float> result(pixels.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::array<double, 4> sum{};
            double total = 0;
            for (int j = -radius; j <= radius; ++j) {
                for (int i = -radius; i <= radius; ++i) {
                    const double weight = std::exp(-(i * i + j * j) / (2 * sigma * sigma));
                    const int xx = std::clamp(x + i, 0, width - 1),
                              yy = std::clamp(y + j, 0, height - 1);
                    total += weight;
                    for (int c = 0; c < 4; ++c) {
                        sum[c] += pixels[(yy * width + xx) * 4 + c] * weight;
                    }
                }
            }
            for (int c = 0; c < 4; ++c) {
                result[(y * width + x) * 4 + c] = float(sum[c] / total);
            }
        }
    }
    return result;
}
} // namespace

int main() {
    test::run("all single-pass operation families honor zero/full masks", [] {
        auto ctx = Context::create();
        auto src = ctx.create_image({3, 2}), original = ctx.create_image({3, 2});
        auto target = ctx.create_image({3, 2}), full = ctx.create_image({3, 2});
        const std::array<float, 24> values{.1f,  .2f, .05f, .5f,  .2f, .1f, .3f, .75f,
                                           0,    .1f, .2f,  .25f, .5f, .2f, .1f, 1,
                                           .05f, .1f, .15f, .5f,  0,   0,   0,   0};
        test::paint(ctx, original, values);
        auto init = ctx.create_commands(1);
        init.fill(src, {.color = {.2f, .05f, .1f, .5f}});
        ctx.submit_and_wait(init);
        auto zero = mask_from(ctx, 3, 2, std::array<std::uint8_t, 6>{0, 0, 0, 0, 0, 0});
        auto one = mask_from(ctx, 3, 2, std::array<std::uint8_t, 6>{255, 255, 255, 255, 255, 255});
        using Apply = std::function<void(Commands&, const Image&, const Mask*)>;
        const Color a{.1f, .2f, .05f, .5f}, b{.4f, .1f, .2f, .75f};
        const std::vector<std::pair<const char*, Apply>> operations{
            {"copy", [&](auto& c, auto& d, auto m) { c.copy(src, d, {.mask = m}); }},
            {"resize",
             [&](auto& c, auto& d, auto m) {
                 c.resize(src, d, {.filter = ResizeFilter::lanczos, .mask = m});
             }},
            {"fill", [&](auto& c, auto& d, auto m) { c.fill(d, {.color = b, .mask = m}); }},
            {"gradient_fill",
             [&](auto& c, auto& d, auto m) {
                 const std::array stops{GradientStop{0, a}, GradientStop{1, b}};
                 c.gradient_fill(d, {.start = {0, 0},
                                     .end = {3, 2},
                                     .stops = stops,
                                     .extend = EdgeMode::mirror,
                                     .dither = false,
                                     .replace = true,
                                     .mask = m});
             }},
            {"checkerboard",
             [&](auto& c, auto& d, auto m) {
                 c.checkerboard(d,
                                {.size = 1, .first = a, .second = b, .offset = {0, 0}, .mask = m});
             }},
            {"grid",
             [&](auto& c, auto& d, auto m) {
                 c.grid(d, {.spacing = 2,
                            .line_width = 1,
                            .color = a,
                            .background = b,
                            .offset = {0, 0},
                            .mask = m});
             }},
            {"stripes",
             [&](auto& c, auto& d, auto m) {
                 c.stripes(d, {.angle = 30,
                               .spacing = 2,
                               .width = 1,
                               .first = a,
                               .second = b,
                               .offset = 0,
                               .mask = m});
             }},
            {"dots",
             [&](auto& c, auto& d, auto m) {
                 c.dots(d, {.spacing = 2,
                            .radius = .5f,
                            .color = a,
                            .background = b,
                            .offset = {0, 0},
                            .softness = .1f,
                            .mask = m});
             }},
            {"circle",
             [&](auto& c, auto& d, auto m) {
                 c.circle(d, {.color = a, .background = b, .softness = .1f, .mask = m});
             }},
            {"noise", [&](auto& c, auto& d,
                          auto m) { c.noise(d, {.seed = 123, .monochrome = false, .mask = m}); }},
            {"perlin",
             [&](auto& c, auto& d, auto m) {
                 c.perlin(d, {.scale = 2,
                              .seed = 1,
                              .octaves = 2,
                              .persistence = .5f,
                              .lacunarity = 2,
                              .first = a,
                              .second = b,
                              .offset_x = 0,
                              .offset_y = 0,
                              .mask = m});
             }},
            {"polygon",
             [&](auto& c, auto& d, auto m) {
                 c.polygon(d, {.sides = 5,
                               .color = a,
                               .background = b,
                               .rotation = 0,
                               .softness = .1f,
                               .mask = m});
             }},
            {"grayscale", [&](auto& c, auto& d, auto m) { c.grayscale(d, {.mask = m}); }},
            {"brightness",
             [&](auto& c, auto& d, auto m) { c.brightness(d, {.amount = .2f, .mask = m}); }},
            {"contrast",
             [&](auto& c, auto& d, auto m) { c.contrast(d, {.factor = 1.4f, .mask = m}); }},
            {"saturation",
             [&](auto& c, auto& d, auto m) { c.saturation(d, {.factor = .2f, .mask = m}); }},
            {"gamma", [&](auto& c, auto& d, auto m) { c.gamma(d, {.value = 1.3f, .mask = m}); }},
            {"opacity",
             [&](auto& c, auto& d, auto m) { c.opacity(d, {.factor = .3f, .mask = m}); }},
            {"hue", [&](auto& c, auto& d, auto m) { c.hue(d, {.degrees = 73, .mask = m}); }},
            {"vibrance",
             [&](auto& c, auto& d, auto m) { c.vibrance(d, {.amount = .3f, .mask = m}); }},
            {"blend",
             [&](auto& c, auto& d, auto m) {
                 c.blend(
                     src, d,
                     {.position = {1, 0}, .opacity = .6f, .mode = BlendMode::normal, .mask = m});
             }},
            {"apply_mask",
             [&](auto& c, auto& d, auto m) {
                 auto matte = ctx.create_mask(src.size());
                 c.extract_mask(src, matte, {.mode = MaskMode::alpha});
                 c.apply_mask(matte, d, {.position = {1, 0}, .mask = m});
             }},
            {"flip",
             [&](auto& c, auto& d, auto m) {
                 c.flip(src, d, {.direction = FlipDirection::both, .mask = m});
             }},
            {"rotate",
             [&](auto& c, auto& d, auto m) {
                 c.rotate(src, d, {.degrees = 31, .filter = ResizeFilter::bicubic, .mask = m});
             }},
            {"crop",
             [&](auto& c, auto& d, auto m) { c.crop(src, d, {.origin = {-1, 0}, .mask = m}); }},
            {"zoom",
             [&](auto& c, auto& d, auto m) {
                 c.zoom(src, d,
                        {.factor = 1.3f,
                         .center = {1, .5f},
                         .filter = ResizeFilter::bilinear,
                         .mask = m});
             }},
            {"sharpen",
             [&](auto& c, auto& d, auto m) { c.sharpen(src, d, {.strength = .5f, .mask = m}); }},
            {"sepia", [&](auto& c, auto& d, auto m) { c.sepia(d, {.intensity = .6f, .mask = m}); }},
            {"invert", [&](auto& c, auto& d, auto m) { c.invert(d, {.mask = m}); }},
            {"threshold",
             [&](auto& c, auto& d, auto m) { c.threshold(d, {.value = .3f, .mask = m}); }},
            {"solarize",
             [&](auto& c, auto& d, auto m) { c.solarize(d, {.value = .3f, .mask = m}); }},
            {"sobel", [&](auto& c, auto& d, auto m) { c.sobel(src, d, {.mask = m}); }},
            {"emboss",
             [&](auto& c, auto& d, auto m) { c.emboss(src, d, {.strength = .3f, .mask = m}); }},
            {"rounded_corners",
             [&](auto& c, auto& d, auto m) { c.rounded_corners(d, {.radius = 1, .mask = m}); }},
            {"vignette",
             [&](auto& c, auto& d, auto m) {
                 c.vignette(d, {.radius = .3f, .softness = .4f, .color = a, .mask = m});
             }},
            {"chroma_key", [&](auto& c, auto& d, auto m) {
                 c.chroma_key(d, {.key = {0, 1, 0, 1},
                                  .threshold = .6f,
                                  .smoothness = .4f,
                                  .spill_suppression = .3f,
                                  .mask = m});
             }}};
        const auto baseline = test::read(ctx, original);
        for (const auto& [name, apply] : operations) {
            auto cmd = ctx.create_commands(6);
            cmd.copy(original, target);
            cmd.copy(original, full);
            apply(cmd, target, &zero);
            apply(cmd, full, nullptr);
            ctx.submit_and_wait(cmd);
            test::check(test::read(ctx, target) == baseline,
                        std::string(name) + " changed pixels under zero mask");
            auto on = ctx.create_commands(3);
            on.copy(original, target);
            apply(on, target, &one);
            ctx.submit_and_wait(on);
            test::check(test::read(ctx, target) == test::read(ctx, full),
                        std::string(name) + " full mask differs from unmasked");
        }
    });
    test::run("intermediate coverage mixes linear premultiplied alpha and HDR", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({4, 1});
        const std::array<std::uint8_t, 4> coverage{0, 64, 128, 255};
        auto mask = mask_from(ctx, 4, 1, coverage);
        const std::array<float, 16> old{.75f, .9f, 1, .5f, .75f, .9f, 1, .5f,
                                        .75f, .9f, 1, .5f, .75f, .9f, 1, .5f};
        std::vector<float> replacement;
        for (int i = 0; i < 4; ++i) {
            replacement.insert(replacement.end(), {.4f, .5f, .6f, .25f});
        }
        test::paint(ctx, image, old);
        auto cmd = ctx.create_commands(2);
        cmd.fill(image, {.color = {.4f, .5f, .6f, .25f}, .mask = &mask});
        cmd.brightness(image, {.amount = -1});
        ctx.submit_and_wait(cmd);
        auto expected = mix(old, replacement, coverage);
        adjust(expected, -1);
        expect(ctx, image, expected);
        test::paint(ctx, image, old);
        auto opacity = ctx.create_commands(2);
        opacity.opacity(image, {.factor = .2f, .mask = &mask});
        opacity.brightness(image, {.amount = -1});
        ctx.submit_and_wait(opacity);
        std::vector<float> faded(old.begin(), old.end());
        for (auto& v : faded) {
            v *= .2f;
        }
        expected = mix(old, faded, coverage);
        adjust(expected, -1);
        expect(ctx, image, expected);
    });
    test::run("geometry uses destination mask coordinates including padding", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({2, 1}), dest = ctx.create_image({4, 1});
        const std::array<float, 8> src{.4f, 0, 0, .5f, 0, .2f, 0, .25f};
        const std::array<float, 16> old{0, 0, .2f, .5f, 0, 0, .2f, .5f,
                                        0, 0, .2f, .5f, 0, 0, .2f, .5f};
        const std::array<std::uint8_t, 4> coverage{255, 0, 128, 255};
        auto mask = mask_from(ctx, 4, 1, coverage);
        test::paint(ctx, source, src);
        test::paint(ctx, dest, old);
        auto cmd = ctx.create_commands(1);
        cmd.crop(source, dest, {.origin = {-1, 0}, .mask = &mask});
        ctx.submit_and_wait(cmd);
        const std::array<float, 16> full{0, 0, 0, 0, .4f, 0, 0, .5f, 0, .2f, 0, .25f, 0, 0, 0, 0};
        expect(ctx, dest, mix(old, full, coverage));
    });
    test::run("blend and apply_mask sample coverage in destination coordinates", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({2, 1}), dest = ctx.create_image({4, 1});
        const std::array<float, 8> src{.4f, 0, 0, .5f, 0, .2f, 0, .25f};
        const std::array<float, 16> old{0, 0, .2f, .5f, 0, 0, .2f, .5f,
                                        0, 0, .2f, .5f, 0, 0, .2f, .5f};
        const std::array<std::uint8_t, 4> coverage{255, 0, 128, 255};
        auto coverage_mask = mask_from(ctx, 4, 1, coverage);
        test::paint(ctx, source, src);
        for (bool alpha_mask : {false, true}) {
            test::paint(ctx, dest, old);
            auto cmd = ctx.create_commands(2);
            if (alpha_mask) {
                auto matte = ctx.create_mask(source.size());
                cmd.extract_mask(source, matte, {.mode = MaskMode::alpha});
                cmd.apply_mask(matte, dest, {.position = {1, 0}, .mask = &coverage_mask});
            } else {
                cmd.blend(source, dest,
                          {.position = {1, 0},
                           .opacity = 1,
                           .mode = BlendMode::normal,
                           .mask = &coverage_mask});
            }
            ctx.submit_and_wait(cmd);
            std::vector<float> full(old.begin(), old.end());
            for (int x = 0; x < 2; ++x) {
                for (int c = 0; c < 4; ++c) {
                    if (alpha_mask) {
                        full[(x + 1) * 4 + c] =
                            old[(x + 1) * 4 + c] * (std::round(src[x * 4 + 3] * 255) / 255);
                    } else {
                        full[(x + 1) * 4 + c] =
                            src[x * 4 + c] + old[(x + 1) * 4 + c] * (1 - src[x * 4 + 3]);
                    }
                }
            }
            expect(ctx, dest, mix(old, full, coverage));
        }
    });
    test::run("blur applies coverage after both passes and samples outside mask", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 3}), temp = ctx.create_image({3, 3});
        const std::array<std::uint8_t, 9> coverage{0, 0, 0, 0, 128, 255, 0, 0, 0};
        auto mask = mask_from(ctx, 3, 3, coverage);
        std::vector<float> old(36, 0);
        // Bright pixel outside the mask must contribute to a masked neighbor.
        old[0] = 1.2f;
        old[1] = .8f;
        old[2] = .4f;
        old[3] = .5f;
        old[4 * 4] = .1f;
        old[4 * 4 + 1] = .2f;
        old[4 * 4 + 2] = .3f;
        old[4 * 4 + 3] = .5f;
        test::paint(ctx, image, old);
        auto options = test::reserve_workspace(ctx, image, GaussianBlurOptions{.radius = 1, .sigma = .9f}, gaussian_blur_requirements);
        auto poison = ctx.create_commands(3);
        poison.fill(temp, {.color = {9, 8, 7, 1}});
        poison.gaussian_blur(temp, options); // Leave unrelated HDR values in the workspace.
        ctx.submit_and_wait(poison);
        options.mask = &mask;
        auto cmd = ctx.create_commands(2);
        cmd.gaussian_blur(image, options);
        ctx.submit_and_wait(cmd);
        expect(ctx, image, mix(old, blur(old, 3, 3, 1, .9), coverage));
        // Alpha and HDR are mixed before export; reveal retained HDR afterwards.
        test::paint(ctx, image, old);
        auto adjusted = ctx.create_commands(3);
        adjusted.gaussian_blur(
            image, options);
        adjusted.brightness(image, {.amount = -.5f});
        ctx.submit_and_wait(adjusted);
        auto expected = mix(old, blur(old, 3, 3, 1, .9), coverage);
        adjust(expected, -.5f);
        expect(ctx, image, expected);
    });
    test::run("mask validation and two-pass blur fail atomically", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 2}), temp = ctx.create_image({3, 2});
        auto wrong =
            mask_from(ctx, 2, 3, std::array<std::uint8_t, 6>{255, 255, 255, 255, 255, 255});
        auto good = mask_from(ctx, 3, 2, std::array<std::uint8_t, 6>{255, 255, 255, 255, 255, 255});
        auto cmd = ctx.create_commands(1);
        test::error(ErrorCode::invalid_argument, "gaussian_blur", "mask",
                    [&] { cmd.gaussian_blur(image, {.radius = 0, .sigma = 1, .mask = &wrong}); });
        test::error(ErrorCode::invalid_argument, "fill", "mask",
                    [&] { cmd.fill(image, {.color = {1, 0, 0, 1}, .mask = &wrong}); });
        auto options = test::reserve_workspace(
            ctx, image, GaussianBlurOptions{.radius = 1, .sigma = 1, .mask = &good},
            gaussian_blur_requirements);
        ctx.set_memory_limit(ctx.memory().total);
        test::error(ErrorCode::capacity, "gaussian_blur", "memory_limit",
                    [&] { cmd.gaussian_blur(image, options); });
        ctx.set_memory_limit(0);
        cmd.fill(image, {.color = {.1f, .2f, .3f, .5f}});
        ctx.submit_and_wait(cmd);
        std::vector<float> expected;
        for (int i = 0; i < 6; ++i) {
            expected.insert(expected.end(), {.1f, .2f, .3f, .5f});
        }
        expect(ctx, image, expected);
        auto valid = ctx.create_commands(2);
        valid.gaussian_blur(image, test::reserve_workspace(
                                       ctx, image,
                                       GaussianBlurOptions{.radius = 1, .sigma = 1, .mask = &good},
                                       gaussian_blur_requirements));
        ctx.submit_and_wait(valid);
        expect(ctx, image, expected);
        auto radius_zero = ctx.create_commands(1);
        radius_zero.gaussian_blur(
            image, test::reserve_workspace(
                       ctx, image, GaussianBlurOptions{.radius = 0, .sigma = 1, .mask = &good},
                       gaussian_blur_requirements));
        radius_zero.brightness(image, {.amount = .1f});
        ctx.submit_and_wait(radius_zero);
        adjust(expected, .1f);
        expect(ctx, image, expected);
    });
    return test::finish();
}
