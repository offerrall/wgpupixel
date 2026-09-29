#include "../test.h"

#include <array>
#include <numbers>

using namespace wgpupixel;

namespace {
double kernel(double x) {
    x = std::abs(x);
    if (x >= 3) {
        return 0;
    }
    if (x < 1e-12) {
        return 1;
    }
    const double p = std::numbers::pi * x;
    return 3 * std::sin(p) * std::sin(p / 3) / (p * p);
}

// Separable double-precision reference, with independent support enumeration.
// Reductions widen the kernel by the reduction factor.
std::vector<float> reference(std::span<const float> source, int sw, int sh, int dw, int dh) {
    const double scale_x = std::max(1.0, double(sw) / dw), scale_y = std::max(1.0, double(sh) / dh);
    std::vector<double> horizontal(std::size_t(dw) * sh * 4);
    for (int y = 0; y < sh; ++y) {
        for (int x = 0; x < dw; ++x) {
            const double center = (x + 0.5) * sw / dw - 0.5;
            double total = 0;
            std::array<double, 4> sum{};
            for (int tap = static_cast<int>(std::ceil(center - 3 * scale_x));
                 tap <= center + 3 * scale_x; ++tap) {
                const double weight = kernel((center - tap) / scale_x);
                total += weight;
                const int index = (y * sw + std::clamp(tap, 0, sw - 1)) * 4;
                for (int c = 0; c < 4; ++c) {
                    sum[c] += source[index + c] * weight;
                }
            }
            for (int c = 0; c < 4; ++c) {
                horizontal[(y * dw + x) * 4 + c] = sum[c] / total;
            }
        }
    }
    std::vector<float> output(std::size_t(dw) * dh * 4);
    for (int y = 0; y < dh; ++y) {
        const double center = (y + 0.5) * sh / dh - 0.5;
        for (int x = 0; x < dw; ++x) {
            double total = 0;
            std::array<double, 4> sum{};
            for (int tap = static_cast<int>(std::ceil(center - 3 * scale_y));
                 tap <= center + 3 * scale_y; ++tap) {
                const double weight = kernel((center - tap) / scale_y);
                total += weight;
                const int index = (std::clamp(tap, 0, sh - 1) * dw + x) * 4;
                for (int c = 0; c < 4; ++c) {
                    sum[c] += horizontal[index + c] * weight;
                }
            }
            for (int c = 0; c < 4; ++c) {
                output[(y * dw + x) * 4 + c] = static_cast<float>(sum[c] / total);
            }
        }
    }
    return output;
}

std::vector<std::uint8_t> verify(Context& ctx, std::span<const float> pixels, int sw, int sh,
                                 int dw, int dh, float brightness = 0) {
    auto source = ctx.create_image({sw, sh});
    auto destination = ctx.create_image({dw, dh});
    test::paint(ctx, source, pixels);
    auto cmd = ctx.create_commands(2);
    cmd.resize(source, destination, {.filter = ResizeFilter::lanczos});
    if (brightness != 0) {
        cmd.brightness(destination, {.amount = brightness});
    }
    ctx.submit_and_wait(cmd);
    auto expected = reference(pixels, sw, sh, dw, dh);
    for (std::size_t i = 0; i < expected.size(); i += 4) {
        for (int c = 0; c < 3; ++c) {
            expected[i + c] += brightness * expected[i + 3];
        }
    }
    const auto encoded = test::encode(expected);
    auto actual = test::read(ctx, destination);
    for (std::size_t i = 0; i < actual.size(); ++i) {
        test::check(std::abs(int(actual[i]) - int(encoded[i])) <= 2,
                    "Lanczos component " + std::to_string(i) + " differs from CPU reference");
    }
    ctx.destroy(source);
    ctx.destroy(destination);
    return actual;
}
} // namespace

int main() {
    test::run("Lanczos semitransparency, identity, odd dimensions and reduction", [] {
        auto ctx = Context::create();
        std::vector<float> pixels;
        for (int y = 0; y < 5; ++y) {
            for (int x = 0; x < 7; ++x) {
                const float a = 0.25f + 0.25f * ((x + y) % 4);
                pixels.insert(pixels.end(), {a * x / 6, a * y / 4, a * 0.3f, a});
            }
        }
        verify(ctx, pixels, 7, 5, 7, 5);
        verify(ctx, pixels, 7, 5, 13, 9);
        verify(ctx, pixels, 7, 5, 3, 1);
        verify(ctx, std::array<float, 4>{0.1f, 0.2f, 0.3f, 0.5f}, 1, 1, 7, 5);
    });
    test::run("Lanczos centered impulse remains symmetric including signed lobes", [] {
        auto ctx = Context::create();
        std::vector<float> pixels(7 * 4, 0);
        pixels[3 * 4] = 0.4f;
        pixels[3 * 4 + 1] = 0.1f;
        pixels[3 * 4 + 3] = 0.5f;
        verify(ctx, pixels, 7, 1, 23, 3);
        for (std::size_t i = 3; i < pixels.size(); i += 4) {
            pixels[i] = 1;
        }
        const auto actual = verify(ctx, pixels, 7, 1, 23, 1, 0.2f);
        for (int x = 0; x < 23; ++x) {
            for (int c = 0; c < 4; ++c) {
                test::check(std::abs(int(actual[x * 4 + c]) - int(actual[(22 - x) * 4 + c])) <= 1,
                            "Lanczos support is asymmetric");
            }
        }
    });
    test::run("Lanczos preserves HDR and negative RGB before subsequent adjustment", [] {
        auto ctx = Context::create();
        verify(ctx, std::array<float, 4>{1.25f, 1, 0.9f, 0.5f}, 1, 1, 5, 3, -2);
        verify(ctx, std::array<float, 4>{-0.25f, -0.1f, 0, 0.5f}, 1, 1, 3, 5, 0.6f);
    });
    return test::finish();
}
