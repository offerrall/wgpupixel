#include "../test.h"
#include <array>
#include <limits>

using namespace wgpupixel;

namespace {
void same_pixels(Context& ctx, const Image& actual, const Image& expected) {
    const auto a = test::read(ctx, actual);
    const auto b = test::read(ctx, expected);
    test::check(a.size() == b.size(), "blend_many output extent differs");
    for (std::size_t i = 0; i < a.size(); ++i) {
        test::near(a[i], b[i], 1);
    }
}

void upload(Context& ctx, const Mask& mask, std::span<const std::uint8_t> bytes) {
    auto buffer = ctx.create_upload_buffer(mask);
    ctx.write(buffer, bytes);
    auto commands = ctx.create_commands(1);
    commands.upload(buffer, mask);
    ctx.submit_and_wait(commands);
    ctx.destroy(buffer);
}
} // namespace

int main() {
    test::run("blend_many matches ordered blend across all modes, masks and regions", [] {
        auto ctx = Context::create();
        std::array unique{ctx.create_image({1, 1}), ctx.create_image({3, 2}),
                          ctx.create_image({2, 4}), ctx.create_image({5, 1}),
                          ctx.create_image({3, 3})};
        for (std::size_t layer = 0; layer < unique.size(); ++layer) {
            std::vector<float> pixels;
            const auto count = unique[layer].size().width * unique[layer].size().height;
            for (std::uint32_t i = 0; i < count; ++i) {
                const float alpha = std::array{0.0f, 0.4f, 0.7f, 1.0f}[(i + layer) % 4];
                pixels.insert(pixels.end(),
                              {alpha * (0.1f + layer * 0.1f), alpha * (0.05f + i * 0.04f),
                               alpha * (0.8f - layer * 0.12f), alpha});
            }
            test::paint(ctx, unique[layer], pixels);
        }
        const std::array sources{unique[1], unique[0], unique[2], unique[3], unique[4],
                                 unique[1], unique[2], unique[3], unique[4]};
        const std::array positions{
            Position{-1, 1},
            Position{0, -1},
            Position{2, 0},
            Position{std::numeric_limits<int>::min(), std::numeric_limits<int>::max()},
            Position{3, 2},
            Position{1, 1},
            Position{-1, 2},
            Position{4, -1},
            Position{0, 0}};
        const std::array opacities{1.0f, 0.0f, 0.25f, 0.7f, 0.3f, 0.9f, 1.0f, 0.5f, 0.01f};
        auto actual = ctx.create_image({7, 5});
        auto expected = ctx.create_image({7, 5});
        auto mask = ctx.create_mask({7, 5});
        std::array<std::uint8_t, 35> bytes;
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            bytes[i] = std::array<std::uint8_t, 5>{0, 64, 128, 192, 255}[i % 5];
        }
        upload(ctx, mask, bytes);
        const Rect region{-2, 1, 6, 3};
        for (std::uint32_t mode = 0; mode <= 11; ++mode) {
            std::array<BlendMode, 9> modes;
            for (std::size_t i = 0; i < modes.size(); ++i) {
                modes[i] = static_cast<BlendMode>(mode == 11 ? i % 11 : mode);
            }
            for (bool masked : {false, true}) {
                for (bool clipped : {false, true}) {
                    auto commands = ctx.create_commands(14);
                    commands.fill(actual, {.color = {0.1f, 0.2f, 0.15f, 0.5f}});
                    commands.fill(expected, {.color = {0.1f, 0.2f, 0.15f, 0.5f}});
                    commands.blend_many(
                        sources, actual,
                        {.positions = positions,
                         .opacities = opacities,
                         .modes = modes,
                         .mask = masked ? &mask : nullptr,
                         .region = clipped ? std::optional<Rect>{region} : std::nullopt});
                    for (std::size_t i = 0; i < sources.size(); ++i) {
                        commands.blend(
                            sources[i], expected,
                            {.position = positions[i],
                             .opacity = opacities[i],
                             .mode = modes[i],
                             .mask = masked ? &mask : nullptr,
                             .region = clipped ? std::optional<Rect>{region} : std::nullopt});
                    }
                    ctx.submit_and_wait(commands);
                    same_pixels(ctx, actual, expected);
                }
            }
        }
        auto defaults = ctx.create_commands(14);
        defaults.fill(actual, {.color = {0, 0, 0, 0}});
        defaults.fill(expected, {.color = {0, 0, 0, 0}});
        defaults.blend_many(sources, actual, {.positions = positions});
        for (std::size_t i = 0; i < sources.size(); ++i) {
            defaults.blend(sources[i], expected, {.position = positions[i]});
        }
        ctx.submit_and_wait(defaults);
        same_pixels(ctx, actual, expected);
    });

    test::run("blend_many source order and transparent backgrounds", [] {
        auto ctx = Context::create();
        auto red = ctx.create_image({1, 1});
        auto green = ctx.create_image({1, 1});
        auto first = ctx.create_image({1, 1});
        auto reverse = ctx.create_image({1, 1});
        const std::array forward{red, green, red, green, red};
        const std::array backward{green, red, green, red, green};
        const std::array<Position, 5> positions{};
        auto commands = ctx.create_commands(8);
        commands.fill(red, {.color = {0.5f, 0, 0, 0.5f}});
        commands.fill(green, {.color = {0, 0.5f, 0, 0.5f}});
        commands.fill(first, {.color = {0, 0, 0, 0}});
        commands.fill(reverse, {.color = {0, 0, 0, 0}});
        commands.blend_many(forward, first, {.positions = positions});
        commands.blend_many(backward, reverse, {.positions = positions});
        ctx.submit_and_wait(commands);
        const auto a = test::read(ctx, first);
        const auto b = test::read(ctx, reverse);
        test::check(a[0] > a[1] && b[1] > b[0], "source order was not preserved");
        test::near(a[3], 247, 1);
        test::near(b[3], 247, 1);
        auto empty = ctx.create_commands(1);
        empty.blend_many({}, first, {.positions = {}});
        ctx.submit_and_wait(empty);
        test::check(test::read(ctx, first) == a, "empty batch changed its destination");
    });

    test::run("blend_many rejects invalid tails atomically", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({1, 1});
        auto destination = ctx.create_image({1, 1});
        auto foreign = Context::create();
        auto foreign_source = foreign.create_image({1, 1});
        auto setup = ctx.create_commands(1);
        setup.fill(source, {.color = {0.5f, 0, 0, 0.5f}});
        ctx.submit_and_wait(setup);
        std::array sources{source, source, source, source, source};
        const std::array<Position, 5> positions{};
        auto commands = ctx.create_commands(2);
        commands.fill(destination, {.color = {0, 0.5f, 0, 0.5f}});
        test::error(ErrorCode::invalid_argument, "blend_many", "positions", [&] {
            commands.blend_many(sources, destination, {.positions = std::span(positions).first(4)});
        });
        const std::array<float, 4> short_opacities{};
        test::error(ErrorCode::invalid_argument, "blend_many", "opacities", [&] {
            commands.blend_many(sources, destination,
                                {.positions = positions, .opacities = short_opacities});
        });
        const std::array<BlendMode, 4> short_modes{};
        test::error(ErrorCode::invalid_argument, "blend_many", "modes", [&] {
            commands.blend_many(sources, destination,
                                {.positions = positions, .opacities = {}, .modes = short_modes});
        });
        for (float invalid : {-0.1f, 1.1f, std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
            std::array opacities{1.0f, 1.0f, 1.0f, 1.0f, invalid};
            test::error(ErrorCode::invalid_argument, "blend_many", "opacities", [&] {
                commands.blend_many(sources, destination,
                                    {.positions = positions, .opacities = opacities});
            });
        }
        std::array<BlendMode, 5> modes{};
        modes.back() = static_cast<BlendMode>(255);
        test::error(ErrorCode::invalid_argument, "blend_many", "modes", [&] {
            commands.blend_many(sources, destination,
                                {.positions = positions, .opacities = {}, .modes = modes});
        });
        for (auto invalid : {Image{}, foreign_source}) {
            sources.back() = invalid;
            test::error(ErrorCode::invalid_resource, "blend_many", "sources", [&] {
                commands.blend_many(sources, destination, {.positions = positions});
            });
        }
        sources.back() = destination;
        test::error(ErrorCode::invalid_argument, "blend_many", "sources",
                    [&] { commands.blend_many(sources, destination, {.positions = positions}); });
        sources.back() = source;
        auto wrong_mask = ctx.create_mask({2, 1});
        test::error(ErrorCode::invalid_argument, "blend_many", "mask", [&] {
            commands.blend_many(
                sources, destination,
                {.positions = positions, .opacities = {}, .modes = {}, .mask = &wrong_mask});
        });
        const Rect invalid_region{0, 0, 1, -1};
        test::error(ErrorCode::invalid_argument, "blend_many", "region", [&] {
            commands.blend_many(
                sources, destination,
                {.positions = positions, .opacities = {}, .modes = {}, .region = invalid_region});
        });
        ctx.set_memory_limit(ctx.memory().total);
        test::error(ErrorCode::capacity, "blend_many", "memory_limit",
                    [&] { commands.blend_many(sources, destination, {.positions = positions}); });
        ctx.set_memory_limit(0);
        commands.blend_many(std::span(sources).first(1), destination,
                            {.positions = std::span(positions).first(1)});
        ctx.submit_and_wait(commands);
        const auto bytes = test::read(ctx, destination);
        const std::array expected{0.5f, 0.25f, 0.0f, 0.75f};
        const auto encoded = test::encode(expected);
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            test::near(bytes[i], encoded[i], 1);
        }
    });

    test::run("blend_many retains every source until cancellation or completion", [] {
        auto ctx = Context::create();
        std::array sources{ctx.create_image({1, 1}), ctx.create_image({1, 1}),
                           ctx.create_image({1, 1}), ctx.create_image({1, 1})};
        auto destination = ctx.create_image({1, 1});
        const std::array layers{sources[0], sources[1], sources[2], sources[3], sources[1]};
        const std::array<Position, 5> positions{};
        auto setup = ctx.create_commands(5);
        for (const auto& source : sources) {
            setup.fill(source, {.color = {0.1f, 0.2f, 0.3f, 0.5f}});
        }
        setup.fill(destination, {.color = {0, 0, 0, 0}});
        ctx.submit_and_wait(setup);
        auto check_busy = [&] {
            for (const auto& source : sources) {
                test::error(ErrorCode::resource_busy, "destroy", "resource",
                            [&] { ctx.destroy(source); });
            }
            test::error(ErrorCode::resource_busy, "destroy", "resource",
                        [&] { ctx.destroy(destination); });
        };
        {
            auto discarded = ctx.create_commands(2);
            discarded.blend_many(layers, destination, {.positions = positions});
            check_busy();
        }
        for (auto& source : sources) {
            source.set_size({1, 1});
        }
        destination.set_size({1, 1});
        auto commands = ctx.create_commands(2);
        commands.blend_many(layers, destination, {.positions = positions});
        check_busy();
        const auto done = ctx.submit(commands);
        check_busy();
        commands = Commands{};
        ctx.wait(done);
        for (const auto& source : sources) {
            ctx.destroy(source);
        }
        ctx.destroy(destination);
    });
    return test::finish();
}
