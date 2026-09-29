#include "../test.h"

#include <array>

using namespace wgpupixel;

namespace {
// Independent double-precision Catmull-Rom weight. Reductions widen the kernel by
// the reduction factor so every source pixel contributes (no aliasing).
double weight(double t) {
    const double x = std::abs(t);
    if (x <= 1) {
        return 1 - 2.5 * x * x + 1.5 * x * x * x;
    }
    return x < 2 ? 2 - 4 * x + 2.5 * x * x - 0.5 * x * x * x : 0;
}

std::vector<float> reference(std::span<const float> source, int sw, int sh, int dw, int dh) {
    const double scale_x = std::max(1.0, double(sw) / dw), scale_y = std::max(1.0, double(sh) / dh);
    std::vector<float> output(std::size_t(dw) * dh * 4);
    for (int y = 0; y < dh; ++y) {
        const double sy = (y + 0.5) * sh / dh - 0.5;
        for (int x = 0; x < dw; ++x) {
            const double sx = (x + 0.5) * sw / dw - 0.5;
            std::array<double, 4> value{};
            double total = 0;
            for (int j = int(std::ceil(sy - 2 * scale_y)); j <= sy + 2 * scale_y; ++j) {
                for (int i = int(std::ceil(sx - 2 * scale_x)); i <= sx + 2 * scale_x; ++i) {
                    const double w = weight((i - sx) / scale_x) * weight((j - sy) / scale_y);
                    const int xx = std::clamp(i, 0, sw - 1), yy = std::clamp(j, 0, sh - 1);
                    total += w;
                    for (int c = 0; c < 4; ++c) {
                        value[c] += source[(yy * sw + xx) * 4 + c] * w;
                    }
                }
            }
            for (int c = 0; c < 4; ++c) {
                output[(y * dw + x) * 4 + c] = static_cast<float>(value[c] / total);
            }
        }
    }
    return output;
}

void verify(Context& ctx, std::span<const float> pixels, int sw, int sh, int dw, int dh,
            float brightness = 0) {
    auto source = ctx.create_image({sw, sh});
    auto destination = ctx.create_image({dw, dh});
    test::paint(ctx, source, pixels);
    auto cmd = ctx.create_commands(2);
    cmd.resize(source, destination, {.filter = ResizeFilter::bicubic});
    if (brightness != 0) {
        cmd.brightness(destination, {.amount = brightness});
    }
    ctx.submit_and_wait(cmd);
    auto expected = reference(pixels, sw, sh, dw, dh);
    if (brightness != 0) {
        for (std::size_t i = 0; i < expected.size(); i += 4) {
            for (int c = 0; c < 3; ++c) {
                expected[i + c] += brightness * expected[i + 3];
            }
        }
    }
    const auto encoded = test::encode(expected);
    const auto actual = test::read(ctx, destination);
    for (std::size_t i = 0; i < actual.size(); ++i) {
        test::check(std::abs(int(actual[i]) - int(encoded[i])) <= 2,
                    "bicubic component " + std::to_string(i) + " differs from CPU reference");
    }
    ctx.destroy(source);
    ctx.destroy(destination);
}
} // namespace

int main() {
    test::run("bicubic pixel centers, odd sizes, edges and semitransparency", [] {
        auto ctx = Context::create();
        std::vector<float> pixels;
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 5; ++x) {
                const float a = 0.25f + 0.25f * ((x + y) % 4);
                pixels.insert(pixels.end(), {a * x / 4, a * y / 2, a * 0.3f, a});
            }
        }
        verify(ctx, pixels, 5, 3, 5, 3);
        verify(ctx, pixels, 5, 3, 9, 7);
        verify(ctx, pixels, 5, 3, 3, 1);
        verify(ctx, std::array<float, 4>{0.1f, 0.2f, 0.3f, 0.5f}, 1, 1, 7, 5);
    });
    test::run("bicubic transparent impulse and signed lobes", [] {
        auto ctx = Context::create();
        std::vector<float> impulse(5 * 3 * 4, 0);
        impulse[(1 * 5 + 2) * 4] = 0.4f;
        impulse[(1 * 5 + 2) * 4 + 1] = 0.1f;
        impulse[(1 * 5 + 2) * 4 + 3] = 0.5f;
        verify(ctx, impulse, 5, 3, 17, 11);
        // Opaque impulse plus brightness makes negative RGB ringing observable.
        for (std::size_t i = 3; i < impulse.size(); i += 4) {
            impulse[i] = 1;
        }
        verify(ctx, impulse, 5, 3, 17, 11, 0.2f);
    });
    test::run("bicubic retains HDR and negative RGB before later adjustment", [] {
        auto ctx = Context::create();
        verify(ctx, std::array<float, 4>{1.25f, 1.0f, 0.9f, 0.5f}, 1, 1, 5, 3, -2.0f);
        verify(ctx, std::array<float, 4>{-0.25f, -0.1f, 0, 0.5f}, 1, 1, 3, 5, 0.6f);
    });
    return test::finish();
}
