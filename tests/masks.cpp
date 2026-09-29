#include "test.h"

#include <array>
#include <limits>

using namespace wgpupixel;

namespace {
template <class Function> void fails(ErrorCode code, Function function) {
    try {
        function();
    } catch (const Error& error) {
        test::check(error.code() == code, "unexpected mask error code");
        return;
    }
    throw std::runtime_error("expected mask validation error");
}

template <class Function> void rejects_format(Function function) {
    try {
        function();
    } catch (const Error& error) {
        test::check(error.code() == ErrorCode::invalid_argument,
                    "mixed transfer formats must be invalid arguments");
        return;
    }
    throw std::runtime_error("mixed mask/RGBA transfer accepted");
}

void upload(Context& ctx, const Mask& mask, std::span<const std::uint8_t> bytes) {
    auto buffer = ctx.create_upload_buffer(mask);
    ctx.write(buffer, bytes);
    auto commands = ctx.create_commands(1);
    commands.upload(buffer, mask);
    ctx.submit_and_wait(commands);
    ctx.destroy(buffer);
}

std::vector<std::uint8_t> read(Context& ctx, const Mask& mask) {
    auto buffer = ctx.create_readback_buffer(mask);
    auto commands = ctx.create_commands(1);
    commands.download(mask, buffer);
    ctx.submit_and_wait(commands);
    std::vector<std::uint8_t> bytes(std::size_t(mask.size().width) * mask.size().height);
    ctx.read(buffer, bytes);
    ctx.destroy(buffer);
    return bytes;
}

void expect_image(Context& ctx, const Image& image, std::span<const float> expected) {
    const auto actual = test::read(ctx, image);
    const auto encoded = test::encode(expected);
    test::check(actual.size() == encoded.size(), "masked image size mismatch");
    for (std::size_t i = 0; i < actual.size(); ++i) {
        test::near(actual[i], encoded[i], 1);
    }
}
} // namespace

int main() {
    test::run("packed mask dispatch covers narrow, wide and partial final rows", [] {
        auto ctx = Context::create();
        for (const auto size : {std::array{1u, 131u}, std::array{131u, 1u}, std::array{3u, 43u},
                                std::array{33u, 35u}, std::array{64u, 64u}}) {
            auto mask = ctx.create_mask({size[0], size[1]});
            auto image = ctx.create_image({size[0], size[1]});
            auto commands = ctx.create_commands(2);
            commands.fill(mask, {.coverage = 0.25f});
            commands.invert(mask);
            ctx.submit_and_wait(commands);
            for (auto byte : read(ctx, mask)) {
                test::near(byte, 191, 0);
            }

            const auto count = std::size_t(size[0]) * size[1];
            std::vector<std::uint8_t> rgba(count * 4, 0);
            std::vector<std::uint8_t> expected(count);
            for (std::size_t i = 0; i < count; ++i) {
                expected[i] = static_cast<std::uint8_t>((i * 73 + 19) % 256);
                rgba[i * 4 + 3] = expected[i];
            }
            auto input = ctx.create_upload_buffer(image);
            ctx.write(input, rgba);
            commands.upload(input, image);
            commands.extract_mask(image, mask, {.mode = MaskMode::alpha});
            ctx.submit_and_wait(commands);
            test::check(read(ctx, mask) == expected, "packed extraction lost or reordered pixels");
            ctx.destroy(input);
            ctx.destroy(image);
            ctx.destroy(mask);
        }
    });

    test::run("mask compact odd-size transfers and logical size snapshots", [] {
        auto ctx = Context::create();
        auto mask = ctx.create_mask({5, 3});
        test::check(mask.size().width == 5 && mask.size().height == 3 &&
                        mask.capacity_pixels() == 15,
                    "wrong mask dimensions or pixel capacity");
        auto input = ctx.create_upload_buffer(mask);
        auto output = ctx.create_readback_buffer(mask);
        test::check(input.capacity_pixels() == 15 && output.capacity_pixels() == 15,
                    "mask transfers must count single-channel pixels");
        const std::array<std::uint8_t, 15> values{0,   1,   2,   63,  64,  127, 128, 129,
                                                  191, 192, 253, 254, 255, 17,  83};
        ctx.write(input, values);
        auto commands = ctx.create_commands(2);
        commands.upload(input, mask);
        commands.download(mask, output);
        mask.set_size({3, 1});
        ctx.submit_and_wait(commands);
        std::array<std::uint8_t, 15> result{};
        ctx.read(output, result);
        test::check(result == values, "mask byte transfer or recorded shape changed");
        const std::array<std::uint8_t, 3> rewrite{255, 0, 128};
        ctx.write(input, rewrite);
        commands.upload(input, mask);
        commands.download(mask, output);
        ctx.submit_and_wait(commands);
        std::array<std::uint8_t, 3> short_result{};
        ctx.read(output, short_result);
        test::check(short_result == rewrite, "unaligned short mask rewrite failed");
        fails(ErrorCode::invalid_argument, [&] { ctx.read(output, result); });
        fails(ErrorCode::capacity, [&] { mask.set_size({4, 4}); });
        test::check(mask.size().width == 3 && mask.size().height == 1 &&
                        mask.capacity_pixels() == 15,
                    "failed mask resize altered dimensions");
    });

    test::run("mask fill quantization copy and inversion", [] {
        auto ctx = Context::create();
        auto source = ctx.create_mask({3, 1});
        auto destination = ctx.create_mask({3, 1});
        for (float coverage : {0.0f, 0.5f, 1.0f}) {
            auto commands = ctx.create_commands(3);
            commands.fill(source, {.coverage = coverage});
            commands.copy(source, destination);
            commands.invert(destination);
            ctx.submit_and_wait(commands);
            const int expected = static_cast<int>(std::floor(coverage * 255 + 0.5f));
            for (auto byte : read(ctx, source)) {
                test::near(byte, expected, 0);
            }
            for (auto byte : read(ctx, destination)) {
                test::near(byte, 255 - expected, 0);
            }
        }
    });

    test::run("mask extraction uses alpha or premultiplied linear Rec709 luminance", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({5, 1});
        auto mask = ctx.create_mask({5, 1});
        const std::array<float, 20> pixels{0.5f, 0, 0,  0.5f, 0,  0.5f, 0, 0.5f, 0, 0,
                                           0,    0, -1, -1,   -1, 1,    2, 2,    2, 1};
        test::paint(ctx, image, pixels);
        auto commands = ctx.create_commands(1);
        commands.extract_mask(image, mask, {.mode = MaskMode::alpha});
        ctx.submit_and_wait(commands);
        test::check(read(ctx, mask) == std::vector<std::uint8_t>({128, 128, 0, 255, 255}),
                    "alpha extraction must preserve linear coverage");
        commands.extract_mask(image, mask);
        ctx.submit_and_wait(commands);
        const auto actual = read(ctx, mask);
        const std::array<int, 5> expected{27, 91, 0, 0, 255};
        for (std::size_t i = 0; i < expected.size(); ++i) {
            test::near(actual[i], expected[i], 1);
        }
    });

    test::run("mask rejection preserves command capacity and transfer format", [] {
        auto ctx = Context::create();
        auto mask = ctx.create_mask({3, 1});
        auto small = ctx.create_mask({1, 1});
        auto image = ctx.create_image({3, 1});
        auto mask_input = ctx.create_upload_buffer(mask);
        auto mask_output = ctx.create_readback_buffer(mask);
        auto rgba_input = ctx.create_upload_buffer(image);
        auto rgba_output = ctx.create_readback_buffer(image);
        const std::array<std::uint8_t, 3> mask_bytes{255, 128, 0};
        const std::array<std::uint8_t, 12> rgba_bytes{255, 0,   0, 255, 0,   255,
                                                      0,   255, 0, 0,   255, 255};
        ctx.write(mask_input, mask_bytes);
        ctx.write(rgba_input, rgba_bytes);
        auto commands = ctx.create_commands(1);
        rejects_format([&] { commands.upload(rgba_input, mask); });
        rejects_format([&] { commands.upload(mask_input, image); });
        rejects_format([&] { commands.download(image, mask_output); });
        rejects_format([&] { commands.download(mask, rgba_output); });
        fails(ErrorCode::invalid_argument, [&] { commands.copy(mask, small); });
        for (float value : {-0.1f, 1.1f, std::numeric_limits<float>::quiet_NaN()}) {
            fails(ErrorCode::invalid_argument, [&] { commands.fill(mask, {.coverage = value}); });
        }
        commands.fill(mask, {.coverage = 1.0f});
        ctx.submit_and_wait(commands);
        test::check(read(ctx, mask) == std::vector<std::uint8_t>({255, 255, 255}),
                    "invalid mask commands must not consume capacity or change results");
        auto tiny_input = ctx.create_upload_buffer(small);
        const std::array<std::uint8_t, 1> one{255};
        ctx.write(tiny_input, one);
        fails(ErrorCode::capacity, [&] { commands.upload(tiny_input, mask); });
        commands.fill(mask, {.coverage = 0.0f});
        ctx.submit_and_wait(commands);
    });

    test::run("masked fill and brightness interpolate premultiplied output", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 1});
        auto mask = ctx.create_mask({3, 1});
        const std::array<std::uint8_t, 3> weights{0, 128, 255};
        upload(ctx, mask, weights);
        auto commands = ctx.create_commands(2);
        commands.fill(image, {.color = {0.1f, 0.2f, 0.3f, 0.5f}});
        commands.fill(image, {.color = {0.8f, 0.4f, 0.2f, 1.0f}, .mask = &mask});
        ctx.submit_and_wait(commands);
        std::array<float, 12> expected{};
        const std::array<float, 4> old{0.1f, 0.2f, 0.3f, 0.5f};
        const std::array<float, 4> color{0.8f, 0.4f, 0.2f, 1.0f};
        for (std::size_t i = 0; i < 3; ++i) {
            const float weight = weights[i] / 255.0f;
            for (std::size_t c = 0; c < 4; ++c) {
                expected[4 * i + c] = old[c] + weight * (color[c] - old[c]);
            }
        }
        expect_image(ctx, image, expected);
        commands.brightness(image, {.amount = -0.2f, .mask = &mask});
        ctx.submit_and_wait(commands);
        for (std::size_t i = 0; i < 3; ++i) {
            for (std::size_t c = 0; c < 3; ++c) {
                expected[4 * i + c] -= 0.2f * expected[4 * i + 3] * weights[i] / 255.0f;
            }
        }
        expect_image(ctx, image, expected);
    });

    test::run("two-image blending samples the mask at destination coordinates", [] {
        auto ctx = Context::create();
        auto source = ctx.create_image({2, 1});
        auto destination = ctx.create_image({3, 1});
        auto mask = ctx.create_mask({3, 1});
        const std::array<std::uint8_t, 3> weights{255, 0, 128};
        upload(ctx, mask, weights);
        auto commands = ctx.create_commands(3);
        commands.fill(source, {.color = {0.5f, 0, 0, 0.5f}});
        commands.fill(destination, {.color = {0, 0, 0.5f, 0.5f}});
        commands.blend(
            source, destination,
            {.position = {1, 0}, .opacity = 1.0f, .mode = BlendMode::normal, .mask = &mask});
        ctx.submit_and_wait(commands);
        const float weight = 128.0f / 255.0f;
        const std::array<float, 12> expected{0,
                                             0,
                                             0.5f,
                                             0.5f,
                                             0,
                                             0,
                                             0.5f,
                                             0.5f,
                                             0.5f * weight,
                                             0,
                                             0.5f - 0.25f * weight,
                                             0.5f + 0.25f * weight};
        expect_image(ctx, destination, expected);
    });

    test::run("masked operations capture mask dimensions while recording", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 1});
        auto mask = ctx.create_mask({3, 1});
        const std::array<std::uint8_t, 3> weights{0, 128, 255};
        upload(ctx, mask, weights);
        auto commands = ctx.create_commands(2);
        commands.fill(image, {.color = {0, 0, 0, 1}});
        commands.fill(image, {.color = {1, 0, 0, 1}, .mask = &mask});
        mask.set_size({1, 3});
        ctx.submit_and_wait(commands);
        const std::array<float, 12> expected{0, 0, 0, 1, 128.0f / 255.0f, 0, 0, 1, 1, 0, 0, 1};
        expect_image(ctx, image, expected);
        test::check(mask.size().width == 1 && mask.size().height == 3,
                    "submission must not overwrite the updated logical mask shape");
    });

    test::run("mask dimensions empty handles and foreign contexts are validated", [] {
        auto ctx = Context::create();
        auto other = Context::create();
        auto image = ctx.create_image({3, 1});
        auto mask = ctx.create_mask({3, 1});
        auto foreign = other.create_mask({3, 1});
        Mask empty;
        auto commands = ctx.create_commands(1);
        fails(ErrorCode::invalid_resource, [&] { (void)empty.size().width; });
        fails(ErrorCode::invalid_resource,
              [&] { commands.brightness(image, {.amount = 1, .mask = &empty}); });
        fails(ErrorCode::invalid_resource,
              [&] { commands.brightness(image, {.amount = 1, .mask = &foreign}); });
        fails(ErrorCode::invalid_resource, [&] { ctx.destroy(foreign); });
        mask.set_size({1, 3});
        fails(ErrorCode::invalid_argument,
              [&] { commands.brightness(image, {.amount = 1, .mask = &mask}); });
        commands.fill(image, {.color = {0.25f, 0.25f, 0.25f, 1}});
        ctx.submit_and_wait(commands);
        const std::array<float, 12> expected{0.25f, 0.25f, 0.25f, 1,     0.25f, 0.25f,
                                             0.25f, 1,     0.25f, 0.25f, 0.25f, 1};
        expect_image(ctx, image, expected);
        const auto alias = mask;
        ctx.destroy(mask);
        fails(ErrorCode::invalid_resource, [&] { (void)alias.size().height; });
    });

    test::run("mask resources and transfers stay protected until completion", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({3, 1});
        auto mask = ctx.create_mask({3, 1});
        auto input = ctx.create_upload_buffer(mask);
        auto output = ctx.create_readback_buffer(mask);
        const std::array<std::uint8_t, 3> values{0, 128, 255};
        std::array<std::uint8_t, 3> result{};
        ctx.write(input, values);
        auto commands = ctx.create_commands(4);
        commands.fill(image, {.color = {0, 0, 0, 0}});
        commands.upload(input, mask);
        commands.fill(image, {.color = {1, 0, 0, 1}, .mask = &mask});
        commands.download(mask, output);
        auto check_busy = [&] {
            fails(ErrorCode::resource_busy, [&] { ctx.destroy(mask); });
            fails(ErrorCode::resource_busy, [&] { ctx.destroy(input); });
            fails(ErrorCode::resource_busy, [&] { ctx.destroy(output); });
            fails(ErrorCode::resource_busy, [&] { ctx.write(input, values); });
            fails(ErrorCode::resource_busy, [&] { ctx.read(output, result); });
        };
        check_busy();
        const auto submission = ctx.submit(commands);
        check_busy();
        ctx.wait(submission);
        ctx.read(output, result);
        test::check(result == values, "mask was not retained through submission");
        ctx.destroy(mask);
        ctx.destroy(input);
        ctx.destroy(output);
    });
    test::run("optional mask alone is retained by recorded and pending image commands", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        auto mask = ctx.create_mask({1, 1});
        auto commands = ctx.create_commands(2);
        commands.fill(image, {.color = {0.25f, 0.25f, 0.25f, 1}});
        commands.fill(mask, {.coverage = 1.0f});
        ctx.submit_and_wait(commands);
        commands.brightness(image, {.amount = 0.25f, .mask = &mask});
        fails(ErrorCode::resource_busy, [&] { ctx.destroy(mask); });
        const auto submission = ctx.submit(commands);
        fails(ErrorCode::resource_busy, [&] { ctx.destroy(mask); });
        ctx.wait(submission);
        const std::array<float, 4> expected{0.5f, 0.5f, 0.5f, 1};
        expect_image(ctx, image, expected);
        ctx.destroy(mask);
    });
    return test::finish();
}
