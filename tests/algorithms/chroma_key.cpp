#include "../test.h"
#include <array>
#include <limits>
using namespace wgpupixel;
namespace {
using Triple = std::array<double, 3>;
Triple hsv(Triple p) {
    const double hi = *std::max_element(p.begin(), p.end()),
                 lo = *std::min_element(p.begin(), p.end()), d = hi - lo;
    double h = 0;
    if (d > 0) {
        if (hi == p[0]) {
            h = (p[1] - p[2]) / d;
        } else if (hi == p[1]) {
            h = 2 + (p[2] - p[0]) / d;
        } else {
            h = 4 + (p[0] - p[1]) / d;
        }
        h *= 60;
        if (h < 0) {
            h += 360;
        }
    }
    return {h, hi > 0 ? d / hi : 0, hi};
}
std::vector<float> reference(std::span<const float> input, Color key, double threshold,
                             double smoothness, double suppression, double brightness = 0) {
    std::vector<float> result(input.begin(), input.end());
    const Triple k = {key.r / key.a, key.g / key.a, key.b / key.a};
    const auto kh = hsv(k);
    for (std::size_t i = 0; i < result.size(); i += 4) {
        double a = input[i + 3];
        if (a == 0) {
            for (int c = 0; c < 4; ++c) {
                result[i + c] = 0;
            }
            continue;
        }
        Triple rgb = {input[i] / a, input[i + 1] / a, input[i + 2] / a};
        if (threshold > 0 && *std::min_element(rgb.begin(), rgb.end()) >= 0) {
            const auto h = hsv(rgb);
            double hd = std::abs(h[0] - kh[0]);
            hd = std::min(hd, 360 - hd) / 180;
            const double sd = h[1] - kh[1], vd = h[2] - kh[2];
            const double distance = std::sqrt(2 * hd * hd + sd * sd + 0.5 * vd * vd);
            if (distance < threshold) {
                double coverage = 0;
                if (smoothness > 0) {
                    const double start = std::max(0.0, threshold - smoothness);
                    const double t = std::clamp((distance - start) / (threshold - start), 0.0, 1.0);
                    coverage = t * t * (3 - 2 * t);
                }
                if (suppression > 0.001 && coverage > 0.1 && coverage < 0.9) {
                    Triple spill{};
                    for (int c = 0; c < 3; ++c) {
                        spill[c] = std::max(0.0, rgb[c] - k[c] * (1 - coverage) * suppression);
                    }
                    auto luma = [](Triple v) {
                        return v[0] * 0.2126 + v[1] * 0.7152 + v[2] * 0.0722;
                    };
                    if (luma(spill) > 0.001) {
                        const double ratio = luma(rgb) / luma(spill);
                        for (int c = 0; c < 3; ++c) {
                            rgb[c] = spill[c] * ratio;
                        }
                    }
                }
                a *= coverage;
            }
        }
        for (int c = 0; c < 3; ++c) {
            result[i + c] = float((rgb[c] + brightness) * a);
        }
        result[i + 3] = float(a);
    }
    return result;
}
void verify(Context& ctx, std::span<const float> pixels, Color key, float threshold,
            float smoothness, float spill, float brightness = 0) {
    auto image = ctx.create_image({std::int64_t(pixels.size() / 4), 1});
    test::paint(ctx, image, pixels);
    auto cmd = ctx.create_commands(2);
    cmd.chroma_key(
        image,
        {.key = key, .threshold = threshold, .smoothness = smoothness, .spill_suppression = spill});
    if (brightness != 0) {
        cmd.brightness(image, {.amount = brightness});
    }
    ctx.submit_and_wait(cmd);
    const auto actual = test::read(ctx, image),
               expected =
                   test::encode(reference(pixels, key, threshold, smoothness, spill, brightness));
    for (std::size_t i = 0; i < actual.size(); ++i) {
        test::check(std::abs(int(actual[i]) - int(expected[i])) <= 2,
                    "chroma key CPU mismatch at " + std::to_string(i));
    }
    ctx.destroy(image);
}
} // namespace
int main() {
    test::run("chroma key primary colors, feather and existing semi-alpha", [] {
        auto ctx = Context::create();
        const std::array<float, 24> pixels{0,    0.5f, 0,     0.5f,  0.5f,  0,    0,    0.5f,
                                           0,    0,    0.25f, 0.25f, 0.05f, 0.4f, 0.1f, 0.5f,
                                           0.2f, 0.3f, 0.05f, 0.5f,  0,     0,    0,    0};
        for (float smoothness : {0.f, 0.5f, 0.8f}) {
            for (float spill : {0.f, 0.7f}) {
                verify(ctx, pixels, {0, 0.5f, 0, 0.5f}, 0.5f, smoothness, spill);
            }
        }
        verify(ctx, pixels, {0, 1, 0, 1}, 0, 0, 1);
    });
    test::run("chroma key spill retains HDR and unkeyed signed colors", [] {
        auto ctx = Context::create();
        const std::array<float, 12> pixels{0.1f, 0.8f, 0.05f, 0.5f,  1,    0,
                                           0,    0.5f, -0.1f, 0.25f, 0.3f, 0.5f};
        verify(ctx, pixels, {0, 1, 0, 0.5f}, 0.6f, 0.6f, 0.8f, -1.0f);
        verify(ctx, pixels, {0, 1, 0, 0.5f}, 0.6f, 0.6f, 0.8f, 0.5f);
    });
    test::run("chroma key black key and achromatic colors have defined HSV", [] {
        auto ctx = Context::create();
        verify(
            ctx,
            std::array<float, 12>{0, 0, 0, 0.5f, 0.05f, 0.05f, 0.05f, 0.5f, 0.4f, 0.4f, 0.4f, 0.5f},
            {0, 0, 0, 1}, 0.3f, 0.3f, 1);
    });
    test::run("chroma key validates before consuming its command slot", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        test::paint(ctx, image, std::array<float, 4>{0, 0.5f, 0, 0.5f});
        auto cmd = ctx.create_commands(1);
        for (Color key : {Color{0, 1, 0, 0}, Color{-1, 1, 0, 1}, Color{0, 1, 0, 2},
                          Color{std::numeric_limits<float>::max(), 0, 0, 0.1f}}) {
            test::error(ErrorCode::invalid_argument, "chroma_key", "key",
                        [&] { cmd.chroma_key(image, {.key = key}); });
        }
        for (float value : {-1.f, std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN()}) {
            test::error(ErrorCode::invalid_argument, "chroma_key", "threshold",
                        [&] { cmd.chroma_key(image, {.key = {0, 1, 0, 1}, .threshold = value}); });
            test::error(ErrorCode::invalid_argument, "chroma_key", "smoothness", [&] {
                cmd.chroma_key(image,
                               {.key = {0, 1, 0, 1}, .threshold = 0.4f, .smoothness = value});
            });
            test::error(ErrorCode::invalid_argument, "chroma_key", "spill_suppression", [&] {
                cmd.chroma_key(image, {.key = {0, 1, 0, 1},
                                       .threshold = 0.4f,
                                       .smoothness = 0.1f,
                                       .spill_suppression = value});
            });
        }
        test::error(ErrorCode::invalid_argument, "chroma_key", "spill_suppression", [&] {
            cmd.chroma_key(image, {.key = {0, 1, 0, 1},
                                   .threshold = 0.4f,
                                   .smoothness = 0.1f,
                                   .spill_suppression = 1.1f});
        });
        cmd.chroma_key(
            image,
            {.key = {0, 1, 0, 1}, .threshold = 0.4f, .smoothness = 0, .spill_suppression = 0});
        ctx.submit_and_wait(cmd);
        const auto actual = test::read(ctx, image);
        test::check(actual == std::vector<std::uint8_t>(4, 0), "matching green was not removed");
    });
    return test::finish();
}
