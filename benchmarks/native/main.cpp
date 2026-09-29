// A small, independently timed C++ resident benchmark. JSON goes to stdout.
#include <wgpupixel.h>
#include <wgpupixel_webgpu.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace wgpupixel;
using Clock = std::chrono::steady_clock;

std::int64_t elapsed(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}
std::string quoted(std::string_view value) {
    std::string result = "\"";
    const char* hex = "0123456789abcdef";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') {
            result += '\\';
            result += c;
        } else if (c < 32) {
            result += "\\u00";
            result += hex[c >> 4];
            result += hex[c & 15];
        } else {
            result += c;
        }
    }
    return result + '"';
}
std::string_view view(WGPUStringView value) {
    return value.data
               ? std::string_view(value.data, value.length == WGPU_STRLEN ? std::strlen(value.data)
                                                                          : value.length)
               : std::string_view{};
}
double linear(double x) {
    return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
}
int encoded(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return static_cast<int>(std::floor(
        255 * (x <= 0.0031308 ? 12.92 * x : 1.055 * std::pow(x, 1 / 2.4) - 0.055) + 0.5));
}

int main(int argc, char** argv) try {
    int width = 256, height = 256, samples = 100, warmup = 10;
    std::string operation = "copy", input;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") {
            std::cout << "wgpupixel_benchmark [--operation copy|brightness|grayscale] "
                         "[--width N] [--height N] [--samples N] [--warmup N] "
                         "[--input tightly-packed-rgba8-file]\n";
            return 0;
        }
        if (++i >= argc) {
            throw std::runtime_error("missing argument value");
        }
        const std::string value = argv[i];
        if (arg == "--operation") {
            operation = value;
        } else if (arg == "--input") {
            input = value;
        } else {
            std::size_t used = 0;
            const int number = std::stoi(value, &used);
            if (used != value.size()) {
                throw std::runtime_error("invalid integer");
            }
            if (arg == "--width") {
                width = number;
            } else if (arg == "--height") {
                height = number;
            } else if (arg == "--samples") {
                samples = number;
            } else if (arg == "--warmup") {
                warmup = number;
            } else {
                throw std::runtime_error("unknown option: " + arg);
            }
        }
    }
    if (width <= 0 || height <= 0 || samples <= 0 || warmup < 0) {
        throw std::runtime_error("dimensions/samples must be positive; warmup nonnegative");
    }
    if (operation != "copy" && operation != "brightness" && operation != "grayscale") {
        throw std::runtime_error("unsupported operation");
    }
    auto start = Clock::now();
    auto ctx = Context::create();
    const auto context_ns = elapsed(start);
    auto source = ctx.create_image({width, height});
    auto work = ctx.create_image({width, height});
    auto upload = ctx.create_upload_buffer(source);
    auto download = ctx.create_readback_buffer(work);
    auto cmd = ctx.create_commands(2);
    const auto count = static_cast<std::size_t>(width) * height * 4;
    std::vector<std::uint8_t> pixels(count), output(count);
    // Deterministic integer fixture, with nonzero variable alpha.
    for (std::size_t p = 0; p < count / 4; ++p) {
        pixels[4 * p] = (p * 17 + 31) % 256;
        pixels[4 * p + 1] = (p * 29 + 63) % 256;
        pixels[4 * p + 2] = (p * 43 + 127) % 256;
        pixels[4 * p + 3] = 64 + p % 192;
    }
    if (!input.empty()) {
        std::ifstream file(input, std::ios::binary);
        if (!file.read(reinterpret_cast<char*>(pixels.data()), count) || file.peek() != EOF) {
            throw std::runtime_error("input must contain exactly width*height*4 RGBA8 bytes");
        }
    }
    std::uint64_t checksum = 14695981039346656037ull;
    for (const auto byte : pixels) {
        checksum ^= byte;
        checksum *= 1099511628211ull;
    }
    ctx.write(upload, pixels);
    cmd.upload(upload, source);
    ctx.submit_and_wait(cmd);
    auto prepare = [&] {
        if (operation == "copy") {
            cmd.fill(work, {.color = {0, 0, 0, 0}});
        } else {
            cmd.copy(source, work);
        }
        ctx.submit_and_wait(cmd);
    };
    auto run = [&] {
        if (operation == "copy") {
            cmd.copy(source, work);
        } else if (operation == "brightness") {
            cmd.brightness(work, {.amount = 0.15f});
        } else {
            cmd.grayscale(work);
        }
        ctx.submit_and_wait(cmd);
    };
    auto validate = [&] {
        cmd.download(work, download);
        ctx.submit_and_wait(cmd);
        ctx.read(download, output);
        int maximum = 0;
        for (std::size_t p = 0; p < count; p += 4) {
            double rgb[] = {linear(pixels[p] / 255.0), linear(pixels[p + 1] / 255.0),
                            linear(pixels[p + 2] / 255.0)};
            if (operation == "brightness") {
                for (auto& v : rgb) {
                    v += 0.15;
                }
            }
            if (operation == "grayscale") {
                const auto gray = rgb[0] * 0.2126 + rgb[1] * 0.7152 + rgb[2] * 0.0722;
                for (auto& v : rgb) {
                    v = gray;
                }
            }
            for (int c = 0; c < 3; ++c) {
                const auto expected = pixels[p + 3] ? encoded(rgb[c]) : 0;
                maximum = std::max(maximum, std::abs(static_cast<int>(output[p + c]) - expected));
            }
            maximum = std::max(maximum, std::abs(static_cast<int>(output[p + 3]) - pixels[p + 3]));
        }
        if (maximum > 1) {
            throw std::runtime_error("CPU oracle validation failed");
        }
        return maximum;
    };
    prepare();
    run();
    validate();
    for (int i = 0; i < warmup; ++i) {
        prepare();
        run();
    }
    std::vector<std::int64_t> times;
    times.reserve(samples);
    for (int i = 0; i < samples; ++i) {
        prepare();
        start = Clock::now();
        run();
        times.push_back(elapsed(start));
    }
    const auto error = validate();
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(webgpu::native_context(ctx).adapter, &info) != WGPUStatus_Success) {
        throw std::runtime_error("adapter info unavailable");
    }
    std::cout << "{\"schema_version\":1,\"backend\":\"wgpupixel_cpp\",\"mode\":\"resident\","
              << "\"operation\":" << quoted(operation) << ",\"width\":" << width
              << ",\"height\":" << height << ",\"warmup\":" << warmup
              << ",\"configuration\":" << quoted(WGPUPIXEL_BUILD_CONFIGURATION)
              << ",\"compiler\":" << quoted(WGPUPIXEL_BUILD_COMPILER)
              << ",\"wgpu_revision\":" << quoted(WGPUPIXEL_BUILD_WGPU_REVISION)
              << ",\"fixture\":" << quoted(input.empty() ? "integer-pattern-v1" : input)
              << ",\"fixture_fnv1a64\":" << quoted(std::to_string(checksum))
              << ",\"timing\":\"CPU steady_clock; recording + submit + wait; preparation excluded\""
              << ",\"brightness_amount\":0.15"
              << ",\"device\":" << quoted(view(info.device))
              << ",\"driver_description\":" << quoted(view(info.description))
              << ",\"webgpu_backend_enum\":" << static_cast<int>(info.backendType)
              << ",\"context_create_ns\":" << context_ns << ",\"validation_max_error_u8\":" << error
              << ",\"status\":\"ok\",\"samples_ns\":[";
    for (std::size_t i = 0; i < times.size(); ++i) {
        std::cout << (i ? "," : "") << times[i];
    }
    std::cout << "]}\n";
    wgpuAdapterInfoFreeMembers(info);
    ctx.destroy(download);
    ctx.destroy(upload);
    ctx.destroy(work);
    ctx.destroy(source);
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
