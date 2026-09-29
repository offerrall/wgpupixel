#pragma once

#include <wgpupixel.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
#include <vector>

namespace test {
// Attach an already reserved workspace without changing the options under test.
template <class Options>
Options with_workspace(Options options, const wgpupixel::Workspace& workspace) {
    options.workspace = workspace;
    return options;
}

inline int failures = 0;

// Test callers reserve GPU workspace before the operation under test records work.
// Queries remain public API calls; numerical reference implementations are unchanged.
template <class Options, class Query>
Options reserve_workspace(wgpupixel::Context& context, const wgpupixel::Image& source,
                          Options options, Query query) {
    options.workspace = context.create_workspace(query(source.size(), options).workspace);
    return options;
}

inline void check(bool condition, std::string_view message,
                  std::source_location where = std::source_location::current()) {
    if (!condition) {
        throw std::runtime_error(std::string(where.file_name()) + ":" +
                                 std::to_string(where.line()) + ": " + std::string(message));
    }
}

inline void near(float actual, float expected, float tolerance = 1e-5f,
                 std::source_location where = std::source_location::current()) {
    check(std::isfinite(actual) && std::abs(actual - expected) <= tolerance,
          "expected " + std::to_string(expected) + ", got " + std::to_string(actual), where);
}

template <class Function>
void error(wgpupixel::ErrorCode code, std::string_view operation, std::string_view parameter,
           Function function) {
    try {
        function();
    } catch (const wgpupixel::Error& caught) {
        check(caught.code() == code, "wrong error code");
        check(caught.operation() == operation, "wrong error operation");
        check(caught.parameter() == parameter, "wrong error parameter");
        check(caught.what()[0] != '\0', "empty error message");
        return;
    }
    throw std::runtime_error("expected wgpupixel::Error from " + std::string(operation));
}

template <class Function> void run(std::string_view name, Function function) {
    try {
        function();
        std::cout << "PASS " << name << '\n';
    } catch (const std::exception& caught) {
        ++failures;
        std::cerr << "FAIL " << name << ": " << caught.what() << '\n';
    }
}

inline std::uint8_t channel(float value) {
    const double linear = std::clamp(static_cast<double>(value), 0.0, 1.0);
    const double srgb =
        linear <= 0.0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1 / 2.4) - 0.055;
    return static_cast<std::uint8_t>(std::floor(srgb * 255 + 0.5));
}

inline std::vector<std::uint8_t> encode(std::span<const float> pixels) {
    std::vector<std::uint8_t> result(pixels.size());
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        const float alpha = pixels[i + 3];
        if (alpha <= 0) {
            continue;
        }
        for (std::size_t c = 0; c < 3; ++c) {
            result[i + c] = channel(pixels[i + c] / alpha);
        }
        result[i + 3] =
            static_cast<std::uint8_t>(std::floor(std::clamp(alpha, 0.0f, 1.0f) * 255 + 0.5f));
    }
    return result;
}

// Construct linear/HDR fixtures on the GPU without quantizing through RGBA8 transfers.
inline void paint(wgpupixel::Context& ctx, const wgpupixel::Image& image,
                  std::span<const float> pixels) {
    auto pixel = ctx.create_image({1, 1});
    auto cmd = ctx.create_commands(pixels.size() / 2 + 1);
    cmd.fill(image, {.color = {0, 0, 0, 0}});
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        cmd.fill(pixel, {.color = {pixels[i], pixels[i + 1], pixels[i + 2], pixels[i + 3]}});
        cmd.blend(pixel, image,
                  {.position = {static_cast<std::int32_t>((i / 4) % image.size().width),
                                static_cast<std::int32_t>((i / 4) / image.size().width)}});
    }
    ctx.submit_and_wait(cmd);
    ctx.destroy(pixel);
}

// Fill every pixel with arbitrary float bits, including values no operation may produce.
inline void garbage(wgpupixel::Context& ctx, const wgpupixel::Image& image,
                    std::array<float, 4> value) {
    const auto count = std::size_t(image.size().width) * image.size().height;
    std::vector<float> pixels(count * 4);
    for (std::size_t i = 0; i < pixels.size(); ++i) pixels[i] = value[i % 4];
    auto buffer = ctx.create_upload_buffer(image, {.format = wgpupixel::TransferFormat::rgba32_float});
    ctx.write(buffer, std::span(reinterpret_cast<const std::uint8_t*>(pixels.data()),
                                pixels.size() * sizeof(float)));
    ctx.run_and_wait([&](wgpupixel::Commands& cmd) { cmd.upload(buffer, image); });
    ctx.destroy(buffer);
}

// Geometry options with a workspace reserved from their requirements query, as an
// editor would before recording.
template <class Options>
Options sized(wgpupixel::Context& ctx, wgpupixel::ImageSize source,
              wgpupixel::ImageSize destination, Options options) {
    using namespace wgpupixel;
    if constexpr (std::is_same_v<Options, ResizeOptions>) {
        options.workspace = ctx.create_workspace(resize_requirements(source, destination, options).workspace);
    } else if constexpr (std::is_same_v<Options, PerspectiveOptions>) {
        options.workspace = ctx.create_workspace(perspective_requirements(source, destination, options).workspace);
    } else {
        options.workspace = ctx.create_workspace(transform_requirements(source, destination, options).workspace);
    }
    return options;
}

inline std::vector<std::uint8_t> read(wgpupixel::Context& ctx, const wgpupixel::Image& image) {
    std::vector<std::uint8_t> pixels(std::size_t(image.size().width) * image.size().height * 4);
    auto buffer = ctx.create_readback_buffer(image);
    auto cmd = ctx.create_commands(1);
    cmd.download(image, buffer);
    ctx.submit_and_wait(cmd);
    ctx.read(buffer, pixels);
    ctx.destroy(buffer);
    return pixels;
}

// Numerical assertions must observe storage values, without SDR clipping/quantization.
inline std::vector<float> read_float(wgpupixel::Context& ctx, const wgpupixel::Image& image) {
    std::vector<float> pixels(std::size_t(image.size().width) * image.size().height * 4);
    auto buffer = ctx.create_readback_buffer(image, {.format = wgpupixel::TransferFormat::rgba32_float});
    ctx.run_and_wait([&](wgpupixel::Commands& cmd) { cmd.download(image, buffer); });
    ctx.read(buffer, {reinterpret_cast<std::uint8_t*>(pixels.data()), pixels.size() * sizeof(float)});
    ctx.destroy(buffer);
    return pixels;
}

inline int finish() {
    return failures == 0 ? 0 : 1;
}
} // namespace test
