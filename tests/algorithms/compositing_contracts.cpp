#include "../test.h"
#include <array>

using namespace wgpupixel;

namespace {
using Pixel = std::array<float, 4>;
std::vector<Pixel> read(Context& ctx, const Image& image) {
    auto buffer = ctx.create_readback_buffer(image, {.format = TransferFormat::rgba32_float});
    ctx.run_and_wait([&](Commands& cmd) { cmd.download(image, buffer); });
    std::vector<Pixel> pixels(image.size().width * image.size().height);
    ctx.read(buffer,
             {reinterpret_cast<std::uint8_t*>(pixels.data()), pixels.size() * sizeof(Pixel)});
    ctx.destroy(buffer);
    return pixels;
}
void expect(Context& ctx, const Image& image, Pixel expected) {
    for (const auto actual : read(ctx, image)) {
        for (int c = 0; c < 4; ++c) {
            test::near(actual[c], expected[c], 3e-5f);
        }
    }
}
} // namespace

int main() {
    auto ctx = Context::create();
    test::run("documented blend values independent of GPU implementation", [&] {
        auto source = ctx.create_image({17, 13}), destination = ctx.create_image({17, 13});
        // Hand-evaluated SDR controls for Cb=(1/4,1/2,3/4), Cs=(3/4,1/2,1/4).
        // Both saturations are 1/2; luminances are .4525 and .5475. Thus hue/color
        // shift Cs by -.095, saturation leaves Cb alone, luminosity shifts Cb by .095.
        const std::array<Pixel, 27> expected{{{.75f, .5f, .25f, 1},
                                              {.1875f, .25f, .1875f, 1},
                                              {.8125f, .75f, .8125f, 1},
                                              {.375f, .5f, .625f, 1},
                                              {.25f, .5f, .25f, 1},
                                              {.75f, .5f, .75f, 1},
                                              {.5f, 0, .5f, 1},
                                              {.625f, .5f, .625f, 1},
                                              {1, 1, 1, 1},
                                              {.375f, .5f, .65625f, 1},
                                              {.625f, .5f, .375f, 1},
                                              {1, 1, 1, 1},
                                              {0, 0, 0, 1},
                                              {0, 0, 0, 1},
                                              {1, 1, 1, 1},
                                              {.75f, .5f, .25f, 1},
                                              {.5f, .5f, .5f, 1},
                                              {.5f, .5f, .5f, 1},
                                              {1, 1, 1, 1},
                                              {0, 0, .5f, 1},
                                              {1.f / 3, 1, 1, 1},
                                              {.25f, .5f, .75f, 1},
                                              {.25f, .5f, .75f, 1},
                                              {.655f, .405f, .155f, 1},
                                              {.25f, .5f, .75f, 1},
                                              {.655f, .405f, .155f, 1},
                                              {.345f, .595f, .845f, 1}}};
        for (std::size_t i = 0; i < expected.size(); ++i) {
            for (bool batched : {false, true}) {
                ctx.run_and_wait([&](Commands& cmd) {
                    cmd.fill(source, {.color = {.75f, .5f, .25f, 1}});
                    cmd.fill(destination, {.color = {.25f, .5f, .75f, 1}});
                    if (batched) {
                        const std::array sources{source};
                        const std::array positions{Position{}};
                        const std::array modes{BlendMode(i)};
                        cmd.blend_many(sources, destination,
                                       {.positions = positions, .modes = modes});
                    } else {
                        cmd.blend(source, destination, {.mode = BlendMode(i)});
                    }
                });
                expect(ctx, destination, expected[i]);
            }
        }
        // W3C cubic branch at Cb=.1, Cs=.9 is .2568 (legacy polynomial: .172).
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.fill(source, {.color = {.9f, .9f, .9f, 1}});
            cmd.fill(destination, {.color = {.1f, .1f, .1f, 1}});
            cmd.blend(source, destination, {.mode = BlendMode::soft_light});
        });
        expect(ctx, destination, {.2568f, .2568f, .2568f, 1});
    });
    test::run("clipping group retains base silhouette through repeated opaque layers", [&] {
        auto base = ctx.create_image({17, 13}), group = ctx.create_image({17, 13});
        auto source = ctx.create_image({17, 13}), document = ctx.create_image({17, 13});
        for (bool batched : {false, true}) {
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.fill(base, {.color = {.5f, 0, 0, .5f}});
                cmd.copy(base, group);
                cmd.fill(source, {.color = {0, 0, 1, 1}});
                if (batched) {
                    const std::array sources{source, source};
                    const std::array positions{Position{}, Position{}};
                    const std::array layers{BlendLayerOptions{.preserve_alpha = true},
                                            BlendLayerOptions{.preserve_alpha = true}};
                    cmd.blend_many(sources, group, {.positions = positions, .layers = layers});
                } else {
                    cmd.blend(source, group, {.preserve_alpha = true});
                    cmd.blend(source, group, {.preserve_alpha = true});
                }
                cmd.fill(document, {.color = {1, 1, 1, 1}});
                cmd.blend(group, document, {});
            });
            // Opaque clipped blue replaces base color, never the base's 50% coverage.
            expect(ctx, group, {0, 0, .5f, .5f});
            expect(ctx, document, {.5f, .5f, 1, 1});
        }
    });
    test::run("neutral layers preserve HDR and negative backdrop values", [&] {
        auto source = ctx.create_image({17, 13}), destination = ctx.create_image({17, 13});
        for (auto mode : {BlendMode::color_burn, BlendMode::linear_burn, BlendMode::divide,
                          BlendMode::subtract, BlendMode::color_dodge, BlendMode::linear_dodge,
                          BlendMode::linear_light, BlendMode::vivid_light}) {
            const float neutral =
                mode == BlendMode::subtract || mode == BlendMode::color_dodge ||
                        mode == BlendMode::linear_dodge
                    ? 0.f
                : mode == BlendMode::linear_light || mode == BlendMode::vivid_light ? .5f
                                                                                    : 1.f;
            for (bool batched : {false, true}) {
                ctx.run_and_wait([&](Commands& cmd) {
                    cmd.fill(destination, {.color = {4, 2, -.5f, 1}});
                    cmd.fill(source, {.color = {neutral, neutral, neutral, 1}});
                    if (batched) {
                        const std::array sources{source};
                        const std::array positions{Position{}};
                        const std::array modes{mode};
                        cmd.blend_many(sources, destination,
                                       {.positions = positions, .modes = modes});
                    } else {
                        cmd.blend(source, destination, {.mode = mode});
                    }
                });
                expect(ctx, destination, {4, 2, -.5f, 1});
            }
        }
        // Comparing two large RGB sums must not turn both into infinity and tie.
        for (auto mode : {BlendMode::darker_color, BlendMode::lighter_color}) {
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.fill(source, {.color = {2e38f, 2e38f, 2e38f, 1}});
                cmd.fill(destination, {.color = {3e38f, 3e38f, 3e38f, 1}});
                cmd.blend(source, destination, {.mode = mode});
            });
            const float value = mode == BlendMode::darker_color ? 2e38f : 3e38f;
            expect(ctx, destination, {value, value, value, 1});
        }
        // Same colors are a fixed point of every non-separable mode, including HDR.
        for (auto mode :
             {BlendMode::hue, BlendMode::saturation, BlendMode::color, BlendMode::luminosity}) {
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.fill(source, {.color = {4, 2, -.5f, 1}});
                cmd.copy(source, destination);
                cmd.blend(source, destination, {.mode = mode});
            });
            expect(ctx, destination, {4, 2, -.5f, 1});
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.fill(source, {.color = {3e38f, 1e38f, -1e38f, 1}});
                cmd.copy(source, destination);
                cmd.blend(source, destination, {.mode = mode});
            });
            for (auto p : read(ctx, destination)) {
                test::near(p[0] / 1e38f, 3, 3e-5f);
                test::near(p[1] / 1e38f, 1, 3e-5f);
                test::near(p[2] / 1e38f, -1, 3e-5f);
                test::near(p[3], 1);
            }
        }
    });
    test::run("HDR burn and dodge keep their direction with non-neutral layers", [&] {
        struct Case {
            BlendMode mode;
            Color backdrop, source;
            Pixel expected;
        };
        // Exact reviewer cases: saturated burn cannot increase 4 or 2, and dodge
        // cannot decrease -.5. In-range channels still burn to 0 or dodge to .4/1.
        const std::array cases{
            Case{BlendMode::color_burn, {4, 2, .5f, 1}, {.5f, .5f, .5f, 1}, {4, 2, 0, 1}},
            Case{BlendMode::vivid_light, {4, 2, .5f, 1}, {.25f, .25f, .25f, 1}, {4, 2, 0, 1}},
            Case{BlendMode::color_dodge,
                 {-.5f, .2f, .5f, 1},
                 {.5f, .5f, .5f, 1},
                 {-.5f, .4f, 1, 1}}};
        for (auto size : {ImageSize{1, 1}, ImageSize{17, 13}}) {
            auto source = ctx.create_image(size), destination = ctx.create_image(size);
            for (const auto& example : cases) {
                for (bool batched : {false, true}) {
                    ctx.run_and_wait([&](Commands& cmd) {
                        cmd.fill(source, {.color = example.source});
                        cmd.fill(destination, {.color = example.backdrop});
                        if (batched) {
                            const std::array sources{source};
                            const std::array positions{Position{}};
                            const std::array modes{example.mode};
                            cmd.blend_many(sources, destination,
                                           {.positions = positions, .modes = modes});
                        } else {
                            cmd.blend(source, destination, {.mode = example.mode});
                        }
                    });
                    expect(ctx, destination, example.expected);
                }
            }
            for (auto mode :
                 {BlendMode::color_burn, BlendMode::color_dodge, BlendMode::vivid_light}) {
                for (float gray : {0.f, .001f, .125f, .375f, .625f, .875f, .999f, 1.f}) {
                    ctx.run_and_wait([&](Commands& cmd) {
                        cmd.fill(source, {.color = {gray, gray, gray, 1}});
                        cmd.fill(destination, {.color = {-2, .37f, 8, 1}});
                        cmd.blend(source, destination, {.mode = mode});
                    });
                    const bool darkens = mode == BlendMode::color_burn ||
                                         (mode == BlendMode::vivid_light && gray <= .5f);
                    const Pixel backdrop{-2, .37f, 8, 1};
                    for (const auto pixel : read(ctx, destination)) {
                        for (int c = 0; c < 3; ++c) {
                            test::check(std::isfinite(pixel[c]) &&
                                            (darkens ? pixel[c] <= backdrop[c] + 1e-6f
                                                     : pixel[c] >= backdrop[c] - 1e-6f),
                                        "burn/dodge direction must hold outside SDR");
                        }
                    }
                }
            }
        }
    });
    test::run("default dissolve is stable across rerenders and command reuse", [&] {
        auto source = ctx.create_image({128, 128}), destination = ctx.create_image({128, 128});
        auto unrelated = ctx.create_image({128, 128});
        auto mask = ctx.create_mask({128, 128});
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.fill(source, {.color = {1, 0, 0, 1}});
            cmd.fill(mask, {.coverage = 1});
        });
        std::vector<Pixel> first;
        // Exact reviewer case: same source and default 50% dissolve, freshly recorded.
        for (int pass = 0; pass < 3; ++pass) {
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.fill(destination, {.color = {0, 0, 0, 0}});
                cmd.blend(source, destination, {.opacity = .5f, .mode = BlendMode::dissolve});
            });
            const auto actual = read(ctx, destination);
            if (pass == 0) {
                first = actual;
            } else {
                test::check(actual == first, "default dissolve must not shimmer on rerender");
            }
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.fill(unrelated, {.color = {0, 0, 0, 0}});
                cmd.blend(source, unrelated, {.opacity = .5f, .mode = BlendMode::dissolve});
                cmd.fill(source, {.color = {1, 0, 0, 1}});
            });
        }
        // Five duplicate sources cross a batch boundary. Switching batching strategy,
        // or recycling the same Commands, must preserve the recorded stack's pattern.
        const std::array sources{source, source, source, source, source};
        const std::array<Position, 5> positions{};
        const std::array opacities{.25f, .25f, .25f, .25f, .25f};
        const std::array modes{BlendMode::dissolve, BlendMode::dissolve, BlendMode::dissolve,
                               BlendMode::dissolve, BlendMode::dissolve};
        std::array<BlendLayerOptions, 5> layers{};
        layers[0].source_mask = &mask;
        auto cmd = ctx.create_commands(8);
        for (int pass = 0; pass < 6; ++pass) {
            cmd.fill(destination, {.color = {0, 0, 0, 0}});
            if (pass % 3 == 0) {
                for (const auto& image : sources) {
                    cmd.blend(image, destination, {.opacity = .25f, .mode = BlendMode::dissolve});
                }
            } else {
                cmd.blend_many(
                    sources, destination,
                    {.positions = positions,
                     .opacities = opacities,
                     .modes = modes,
                     .layers = pass % 3 == 1 ? std::span<const BlendLayerOptions>{} : layers});
            }
            ctx.submit_and_wait(cmd);
            const auto actual = read(ctx, destination);
            if (pass == 0) {
                first = actual;
                double sum = 0;
                for (auto p : first) {
                    sum += p[3];
                }
                // Independent 25% coverages: 1 - (3/4)^5 = .7626953125.
                test::near(sum / first.size(), .7626953125f, .015f);
            } else {
                test::check(actual == first,
                            "duplicate layer seeds must survive batching and reuse");
            }
        }
    });
    test::run("dissolve layers have independent coverage and explicit seeds repeat", [&] {
        auto source = ctx.create_image({256, 256}), destination = ctx.create_image({256, 256});
        ctx.run_and_wait([&](Commands& cmd) { cmd.fill(source, {.color = {1, 0, 0, 1}}); });
        auto second = ctx.create_image({256, 256});
        ctx.run_and_wait([&](Commands& cmd) { cmd.copy(source, second); });
        const std::array sources{source, source};
        const std::array positions{Position{}, Position{}};
        const std::array opacities{.5f, .5f};
        const std::array modes{BlendMode::dissolve, BlendMode::dissolve};
        for (int route = 0; route < 4; ++route) {
            ctx.run_and_wait(
                [&](Commands& cmd) { cmd.fill(destination, {.color = {0, 0, 0, 0}}); });
            if (route == 3) {
                for (int i = 0; i < 2; ++i) {
                    ctx.run_and_wait([&](Commands& cmd) {
                        cmd.blend(i == 0 ? source : second, destination,
                                  {.opacity = .5f, .mode = BlendMode::dissolve});
                    });
                }
            } else {
                ctx.run_and_wait([&](Commands& cmd) {
                    if (route == 2) {
                        cmd.blend(source, destination,
                                  {.opacity = .5f, .mode = BlendMode::dissolve});
                        cmd.blend(source, destination,
                                  {.opacity = .5f, .mode = BlendMode::dissolve});
                    } else {
                        const std::array<BlendLayerOptions, 2> layers{};
                        cmd.blend_many(
                            sources, destination,
                            {.positions = positions,
                             .opacities = opacities,
                             .modes = modes,
                             .layers = route == 0 ? std::span<const BlendLayerOptions>{} : layers});
                    }
                });
            }
            double sum = 0;
            for (auto p : read(ctx, destination)) {
                test::check(p[3] == 0 || p[3] == 1, "dissolve coverage must be binary");
                sum += p[3];
            }
            // Two independent Bernoulli(1/2) layers cover 1-(1/2)^2 = 3/4.
            test::near(sum / 65536, .75f, .012f);
        }
        std::vector<Pixel> first;
        for (int route = 0; route < 2; ++route) {
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.fill(destination, {.color = {0, 0, 0, 0}});
                if (route == 0) {
                    cmd.blend(source, destination,
                              {.opacity = .5f, .mode = BlendMode::dissolve, .seed = 0});
                    cmd.blend(source, destination,
                              {.opacity = .5f, .mode = BlendMode::dissolve, .seed = 0});
                } else {
                    const std::array layers{BlendLayerOptions{.seed = 0},
                                            BlendLayerOptions{.seed = 0}};
                    cmd.blend_many(sources, destination,
                                   {.positions = positions,
                                    .opacities = opacities,
                                    .modes = modes,
                                    .layers = layers});
                }
            });
            const auto actual = read(ctx, destination);
            if (route == 0) {
                first = actual;
            } else {
                test::check(actual == first, "explicit seeds must match across batching paths");
            }
        }
    });
    test::run("default and seed-only layers retain eight-source two-record batching", [&] {
        auto source = ctx.create_image({17, 13}), destination = ctx.create_image({17, 13});
        ctx.run_and_wait([&](Commands& cmd) { cmd.fill(source, {.color = {0, 0, 1, 1}}); });
        const std::array sources{source, source, source, source, source, source, source, source};
        const std::array<Position, 8> positions{};
        std::array<BlendLayerOptions, 8> layers{};
        for (bool seeded : {false, true}) {
            if (seeded) {
                for (auto& layer : layers) {
                    layer.seed = 42;
                }
            }
            auto cmd = ctx.create_commands(2);
            cmd.blend_many(sources, destination, {.positions = positions, .layers = layers});
            ctx.submit_and_wait(cmd);
            expect(ctx, destination, {0, 0, 1, 1});
        }
    });
    test::run("Blend If alone uses selected encoding and documented transparent gray", [&] {
        auto source = ctx.create_image({17, 13}), destination = ctx.create_image({17, 13});
        // IEC sRGB gray 128 decodes to .2158605 linear, safely above encoded .5
        // but below linear .5. A hard underlying threshold distinguishes encodings.
        for (auto encoding : {ColorEncoding::linear, ColorEncoding::srgb}) {
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.fill(source, {.color = {1, 0, 0, 1}});
                cmd.fill(destination, {.color = {.2158605f, .2158605f, .2158605f, 1}});
                cmd.blend(
                    source, destination,
                    {.blend_if = BlendIfOptions{.destination = {.black = .5f, .black_split = .5f},
                                                .encoding = encoding}});
            });
            expect(ctx, destination,
                   encoding == ColorEncoding::srgb ? Pixel{1, 0, 0, 1}
                                                   : Pixel{.2158605f, .2158605f, .2158605f, 1});
        }
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.fill(destination, {.color = {0, 0, 0, 0}});
            cmd.blend(
                source, destination,
                {.blend_if = BlendIfOptions{.destination = {.black = .1f, .black_split = .1f}}});
        });
        expect(ctx, destination, {0, 0, 0, 0});
        // Linear gray .25 is halfway across the [0,.5] split: 50% red over gray.
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.fill(destination, {.color = {.25f, .25f, .25f, 1}});
            cmd.blend(source, destination,
                      {.blend_if = BlendIfOptions{.destination = {.black_split = .5f}}});
        });
        expect(ctx, destination, {.625f, .125f, .125f, 1});
        auto cmd = ctx.create_commands();
        test::error(ErrorCode::invalid_argument, "blend", "blend_if.encoding", [&] {
            cmd.blend(source, destination,
                      {.blend_if = BlendIfOptions{.encoding = ColorEncoding(2)}});
        });
    });
    return test::finish();
}
