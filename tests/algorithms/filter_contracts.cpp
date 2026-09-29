#include "../test.h"
#include <array>
#include <random>
using namespace wgpupixel;
namespace {
using Pixel = std::array<float, 4>;
using Pixels = std::vector<Pixel>;
void put(Context& ctx, const Image& image, const Pixels& pixels) {
    auto upload = ctx.create_upload_buffer(image, {.format = TransferFormat::rgba32_float});
    ctx.write(upload, {reinterpret_cast<const std::uint8_t*>(pixels.data()),
                       pixels.size() * sizeof(Pixel)});
    ctx.run_and_wait([&](Commands& cmd) { cmd.upload(upload, image); });
    ctx.destroy(upload);
}
Pixels get(Context& ctx, const Image& image) {
    Pixels pixels(image.size().width * image.size().height);
    auto readback = ctx.create_readback_buffer(image, {.format = TransferFormat::rgba32_float});
    ctx.run_and_wait([&](Commands& cmd) { cmd.download(image, readback); });
    ctx.read(readback,
             {reinterpret_cast<std::uint8_t*>(pixels.data()), pixels.size() * sizeof(Pixel)});
    ctx.destroy(readback);
    return pixels;
}
void exact(const Pixel& actual, const Pixel& expected) {
    test::check(actual == expected, "component-wise median must select the exact input value");
}
} // namespace
int main() {
    test::run("flat HDR and dark LDR windows remain exact", [] {
        auto ctx = Context::create();
        constexpr int w = 64, h = 16;
        auto source = ctx.create_image({w, h}), destination = ctx.create_image({w, h});
        for (auto [flat, outlier] : {std::pair{.3f, 1000.f}, std::pair{.3f, 1.f},
                                     std::pair{.004f, 1.f}, std::pair{-.3f, -1000.f}}) {
            Pixels input(w * h, Pixel{flat, flat, flat, 1});
            input.front() = {0, 0, 0, 1};
            input.back() = {outlier, outlier, outlier, 1};
            put(ctx, source, input);
            for (int radius : {2, 3, 5, 10}) {
                auto workspace = ctx.create_workspace(median_requirements(source.size(), {.radius = radius}).workspace);
                ctx.run_and_wait(
                    [&](Commands& cmd) { cmd.median(source, destination, {.radius = radius, .workspace = workspace}); });
                const auto actual = get(ctx, destination);
                exact(actual[8 * w + 32], {flat, flat, flat, 1});
                for (int y = 0; y < h; ++y) {
                    for (int x = radius + 1; x < w - radius - 1; ++x) {
                        exact(actual[y * w + x], {flat, flat, flat, 1});
                    }
                }
            }
        }
    });
    test::run("random windows match independent nth_element exactly", [] {
        auto ctx = Context::create();
        constexpr int w = 97, h = 61;
        auto source = ctx.create_image({w, h}), destination = ctx.create_image({w, h});
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> random(0, 1);
        Pixels input(w * h);
        for (auto& p : input) {
            p = {random(rng), random(rng), random(rng), 1};
        }
        put(ctx, source, input);
        for (int radius : {2, 3, 5}) {
            auto workspace = ctx.create_workspace(median_requirements(source.size(), {.radius = radius}).workspace);
            ctx.run_and_wait(
                [&](Commands& cmd) { cmd.median(source, destination, {.radius = radius, .workspace = workspace}); });
            const auto actual = get(ctx, destination);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    Pixel expected{};
                    for (int channel = 0; channel < 4; ++channel) {
                        std::vector<float> values;
                        for (int dy = -radius; dy <= radius; ++dy) {
                            for (int dx = -radius; dx <= radius; ++dx) {
                                values.push_back(input[std::clamp(y + dy, 0, h - 1) * w +
                                                       std::clamp(x + dx, 0, w - 1)][channel]);
                            }
                        }
                        auto middle = values.begin() + values.size() / 2;
                        std::nth_element(values.begin(), middle, values.end());
                        expected[channel] = *middle;
                    }
                    exact(actual[y * w + x], expected);
                }
            }
        }
    });
    test::run("default command capacity records a 24 MP median", [] {
        auto ctx = Context::create();
        // A 24 MP float image needs 384 MB; conformant devices may bind only 128 MiB
        // (lavapipe). There, a 300 x 1 strip of 256 x 256 dispatch tiles still exceeds
        // the default capacity of 256 commands.
        const ImageSize size = ctx.limits().max_image_pixels >= 6000 * 4000
                                   ? ImageSize{6000, 4000}
                                   : ImageSize{300 * 256, 8};
        auto source = ctx.create_image(size), destination = ctx.create_image(size);
        auto cmd = ctx.create_commands();
        const auto before = ctx.memory().internal;
        cmd.median(source, destination, {.radius = 2});
        test::check(ctx.memory().internal > before,
                    "fixture must exceed default capacity");
        ctx.submit_and_wait(cmd);
        // Reuse the automatically grown recorder and its uniform storage.
        cmd.median(source, destination, {.radius = 2});
        ctx.submit_and_wait(cmd);
    });
    test::run("bounded round dispatches preserve disk geometry and selections", [] {
        auto ctx = Context::create();
        constexpr int w = 513, h = 521, cx = 255, cy = 255, radius = 10;
        auto source = ctx.create_image({w, h}), destination = ctx.create_image({w, h}),
             scratch = ctx.create_image({w, h});
        auto mask = ctx.create_mask({w, h});
        ctx.run_and_wait([&](Commands& cmd) { cmd.fill(mask, {.coverage = 128.f / 255}); });
        for (bool minimum : {false, true}) {
            Pixels input(w * h, minimum ? Pixel{1, 1, 1, 1} : Pixel{});
            input[cy * w + cx] = minimum ? Pixel{} : Pixel{1, 1, 1, 1};
            put(ctx, source, input);
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.fill(destination, {.color = {.2f, .4f, .6f, 1}});
                MorphologyOptions options{.radius = radius,
                                          .shape = MorphologyShape::round,
                                          .mask = &mask,
                                          .region = Rect{247, 249, 29, 28}};
                if (minimum) {
                    cmd.minimum(
                        source, destination,
                        test::reserve_workspace(ctx, source, options, minimum_requirements));
                } else {
                    cmd.maximum(
                        source, destination,
                        test::reserve_workspace(ctx, source, options, maximum_requirements));
                }
            });
            const auto actual = get(ctx, destination);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    Pixel expected{.2f, .4f, .6f, 1};
                    if (x >= 247 && x < 276 && y >= 249 && y < 277) {
                        const bool disk =
                            (x - cx) * (x - cx) + (y - cy) * (y - cy) <= radius * radius;
                        const float value = (disk != minimum) ? 1 : 0;
                        for (auto& c : expected) {
                            c += (value - c) * (128.f / 255);
                        }
                    }
                    for (int c = 0; c < 4; ++c) {
                        test::near(actual[y * w + x][c], expected[c]);
                    }
                }
            }
        }
    });
    test::run("bounded box segments match analytic clamped ramps", [] {
        auto ctx = Context::create();
        constexpr int w = 2049, h = 513, radius = 1024;
        auto source = ctx.create_image({w, h}), destination = ctx.create_image({w, h}),
             scratch = ctx.create_image({w, h});
        Pixels input(w * h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                input[y * w + x] = {x * .01f, y * .01f, -x * .001f, 1};
            }
        }
        put(ctx, source, input);
        auto mask = ctx.create_mask({w, h});
        ctx.run_and_wait([&](Commands& cmd) { cmd.fill(mask, {.coverage = 128.f / 255}); });
        ctx.run_and_wait([&](Commands& cmd) {
            cmd.fill(destination, {.color = {.2f, .4f, .6f, 1}});
            cmd.box_blur(
                source, destination,
                test::reserve_workspace(ctx, source,
                                        BoxBlurOptions{.radius = radius,
                                                       .mask = &mask,
                                                       .region = Rect{245, 240, 1569, 259}},
                                        box_blur_requirements));
        });
        const auto mean = [](int position, int extent) {
            const int lo = std::max(0, position - radius),
                      hi = std::min(extent - 1, position + radius);
            const auto total = double(lo + hi) * (hi - lo + 1) / 2 +
                               double(std::max(0, position + radius - extent + 1)) * (extent - 1);
            return float(total / (2 * radius + 1));
        };
        const auto actual = get(ctx, destination);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                Pixel expected{.2f, .4f, .6f, 1};
                if (x >= 245 && x < 1814 && y >= 240 && y < 499) {
                    Pixel filtered{mean(x, w) * .01f, mean(y, h) * .01f, -mean(x, w) * .001f, 1};
                    for (int c = 0; c < 4; ++c) {
                        expected[c] += (filtered[c] - expected[c]) * (128.f / 255);
                    }
                }
                for (int c = 0; c < 4; ++c) {
                    test::near(actual[y * w + x][c], expected[c], 2e-5f);
                }
            }
        }
    });
    test::run("ordered batches retain dependencies and async resources", [] {
        auto ctx = Context::create();
        constexpr int w = 513, h = 521, cx = 255, cy = 255;
        auto source = ctx.create_image({w, h}), destination = ctx.create_image({w, h}),
             scratch = ctx.create_image({w, h});
        Pixels input(w * h);
        input[cy * w + cx] = {1, 1, 1, 1};
        put(ctx, source, input);
        auto cmd = ctx.create_commands();
        cmd.maximum(source, destination,
                    test::reserve_workspace(
                        ctx, source,
                        MorphologyOptions{.radius = 10, .shape = MorphologyShape::round},
                        maximum_requirements));
        cmd.minimum(destination, source,
                    test::reserve_workspace(
                        ctx, destination,
                        MorphologyOptions{.radius = 10, .shape = MorphologyShape::round},
                        minimum_requirements));
        const auto flight = ctx.submit(cmd);
        test::error(ErrorCode::resource_busy, "destroy", "resource", [&] { ctx.destroy(source); });
        cmd = {};
        ctx.wait(flight);
        const auto actual = get(ctx, source);
        for (std::size_t i = 0; i < actual.size(); ++i) {
            exact(actual[i], input[i]);
        }
    });
    return test::finish();
}
