#include "../test.h"
#include <array>
#include <cmath>

using namespace wgpupixel;

namespace {
using Pixel = std::array<float, 4>;

void expect(Context& ctx, const Image& image, std::span<const float> expected) {
    const auto actual = test::read(ctx, image);
    const auto encoded = test::encode(expected);
    for (std::size_t i = 0; i < actual.size(); ++i) {
        test::check(std::abs(int(actual[i]) - int(encoded[i])) <= 1,
                    "incorrect additive blend component " + std::to_string(i));
    }
}

Pixel compose(Pixel source, Pixel destination, float opacity) {
    const double source_alpha = source[3] * opacity;
    const double dest_alpha = destination[3];
    Pixel result{};
    result[3] = static_cast<float>(source_alpha + dest_alpha * (1 - source_alpha));
    for (std::size_t c = 0; c < 3; ++c) {
        const double foreground = source[3] == 0 ? 0 : source[c] / source[3];
        const double backdrop = dest_alpha == 0 ? 0 : destination[c] / dest_alpha;
        result[c] = static_cast<float>(source_alpha * (1 - dest_alpha) * foreground +
                                       dest_alpha * (1 - source_alpha) * backdrop +
                                       source_alpha * dest_alpha * (foreground + backdrop));
    }
    return result;
}
} // namespace

int main() {
    test::run("add composes overlap and alpha with opacity", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({1, 1});
        auto destination = ctx.create_image({1, 1});
        for (const Pixel foreground : {Pixel{0.3f, 0.05f, 0.2f, 0.5f}, Pixel{0, 0, 0, 0}}) {
            for (const Pixel backdrop : {Pixel{0.1f, 0.2f, 0.05f, 0.25f}, Pixel{0, 0, 0, 0}}) {
                for (float opacity : {0.0f, 0.4f, 1.0f}) {
                    test::paint(ctx, source, foreground);
                    test::paint(ctx, destination, backdrop);
                    auto cmd = ctx.create_commands(1);
                    cmd.blend(source, destination,
                              {.position = {}, .opacity = opacity, .mode = BlendMode::add});
                    ctx.submit_and_wait(cmd);
                    expect(ctx, destination, compose(foreground, backdrop, opacity));
                    expect(ctx, source, foreground);
                }
            }
        }
    });
    test::run("add preserves HDR and negative overlap colors", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({1, 1});
        auto destination = ctx.create_image({1, 1});
        for (float sign : {-1.0f, 1.0f}) {
            const Pixel foreground{sign * 0.7f, sign * 0.65f, sign * 0.75f, 0.5f};
            const Pixel backdrop{sign * 0.6f, sign * 0.75f, sign * 0.7f, 0.5f};
            test::paint(ctx, source, foreground);
            test::paint(ctx, destination, backdrop);
            auto cmd = ctx.create_commands(2);
            cmd.blend(source, destination,
                      {.position = {}, .opacity = 1.0f, .mode = BlendMode::add});
            const float shift = sign > 0 ? -1.0f : 2.0f;
            cmd.brightness(destination, {.amount = shift});
            ctx.submit_and_wait(cmd);
            auto expected = compose(foreground, backdrop, 1.0f);
            for (std::size_t c = 0; c < 3; ++c) {
                expected[c] += shift * expected[3];
            }
            expect(ctx, destination, expected);
        }
    });
    test::run("add clips negative placement to the destination", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({2, 1});
        auto destination = ctx.create_image({2, 1});
        const std::array<float, 8> foreground{0.5f, 0, 0, 0.5f, 0, 0.2f, 0.1f, 0.5f};
        const std::array<float, 8> backdrop{0.1f, 0.2f, 0.3f, 0.5f, 0.3f, 0.2f, 0.1f, 0.5f};
        test::paint(ctx, source, foreground);
        test::paint(ctx, destination, backdrop);
        auto cmd = ctx.create_commands(1);
        cmd.blend(source, destination,
                  {.position = {-1, 0}, .opacity = 1.0f, .mode = BlendMode::add});
        ctx.submit_and_wait(cmd);
        const std::array<float, 8> expected{0.1f, 0.4f, 0.4f, 0.75f, 0.3f, 0.2f, 0.1f, 0.5f};
        expect(ctx, destination, expected);
    });
    test::run("add keeps finite HDR sums with tiny alpha and fractional masks", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({3, 1});
        auto destination = ctx.create_image({3, 1});
        auto mask = ctx.create_mask({3, 1});
        auto upload = ctx.create_upload_buffer(mask);
        const std::array<std::uint8_t, 3> coverage{0, 128, 255};
        ctx.write(upload, coverage);
        auto setup = ctx.create_commands(1);
        setup.upload(upload, mask);
        ctx.submit_and_wait(setup);
        const Pixel foreground{1e20f, -1e20f, 5e19f, 1e-20f};
        const Pixel backdrop{0.1f, 0.2f, 0.3f, 1};
        for (bool masked : {false, true}) {
            for (float opacity : {0.f, 0.25f, 1.f}) {
                auto cmd = ctx.create_commands(5);
                cmd.fill(source,
                         {.color = {foreground[0], foreground[1], foreground[2], foreground[3]}});
                cmd.fill(destination,
                         {.color = {backdrop[0], backdrop[1], backdrop[2], backdrop[3]}});
                cmd.blend(source, destination,
                          {.position = {},
                           .opacity = opacity,
                           .mode = BlendMode::add,
                           .mask = masked ? &mask : nullptr});
                // The opaque backdrop preserves alpha during this observation.
                // Map both signs into SDR only after the additive operation.
                cmd.exposure(destination, {.stops = -68});
                cmd.brightness(destination, {.amount = 0.5f});
                ctx.submit_and_wait(cmd);
                std::array<float, 12> expected{};
                for (std::size_t x = 0; x < 3; ++x) {
                    const double weight = masked ? double(coverage[x]) / 255 : 1;
                    for (std::size_t c = 0; c < 3; ++c) {
                        const double sum =
                            double(backdrop[c]) + double(foreground[c]) * double(opacity) * weight;
                        expected[x * 4 + c] = float(std::ldexp(sum, -68) + 0.5);
                    }
                    expected[x * 4 + 3] = 1;
                }
                expect(ctx, destination, expected);
            }
        }
    });
    return test::finish();
}
