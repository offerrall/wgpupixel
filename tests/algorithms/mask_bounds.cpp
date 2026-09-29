#include "analysis.h"
#include <chrono>
#include <limits>
#include <thread>

using namespace wgpupixel;
namespace {
// Brute force in source coordinates, independently of packed words, dispatch chunks,
// or the GPU's complemented-minimum representation.
std::optional<Rect> reference(std::span<const std::uint8_t> pixels, int width,
                              const MaskBoundsOptions& options = {}) {
    int left = width, top = int(pixels.size() / width), right = -1, bottom = -1;
    for (int y = 0; y < int(pixels.size() / width); ++y) {
        for (int x = 0; x < width; ++x) {
            if (options.region) {
                const auto r = *options.region;
                if (x < r.x || y < r.y || std::int64_t(x) >= std::int64_t(r.x) + r.width ||
                    std::int64_t(y) >= std::int64_t(r.y) + r.height) continue;
            }
            if (pixels[std::size_t(y) * width + x] < options.threshold) continue;
            left = std::min(left, x);
            right = std::max(right, x);
            top = std::min(top, y);
            bottom = std::max(bottom, y);
        }
    }
    if (right < 0) return std::nullopt;
    return Rect{left, top, right - left + 1, bottom - top + 1};
}

void expect(const std::optional<Rect>& actual, const std::optional<Rect>& expected) {
    test::check(bool(actual) == bool(expected), "wrong mask emptiness");
    if (!expected) return;
    test::check(actual->x == expected->x && actual->y == expected->y &&
                    actual->width == expected->width && actual->height == expected->height,
                "wrong mask bounds: got " + std::to_string(actual->x) + "," +
                    std::to_string(actual->y) + "," + std::to_string(actual->width) + "," +
                    std::to_string(actual->height) + "; expected " + std::to_string(expected->x) +
                    "," + std::to_string(expected->y) + "," + std::to_string(expected->width) +
                    "," + std::to_string(expected->height));
}

void check(Context& ctx, Commands& cmd, const Mask& mask, const MaskBoundsBuffer& result,
           std::span<const std::uint8_t> pixels, const MaskBoundsOptions& options = {}) {
    const auto revision = mask.revision();
    cmd.mask_bounds(mask, result, options);
    ctx.submit_and_wait(cmd);
    expect(ctx.read(result), reference(pixels, int(mask.size().width), options));
    test::check(mask.revision() == revision, "measurement modified the mask revision");
}

void benchmark() {
    auto ctx = Context::create();
    auto mask = ctx.create_mask({6000, 4000});
    auto result = ctx.create_mask_bounds_buffer();
    auto cmd = ctx.create_commands(64);
    cmd.fill(mask, {.coverage = 1});
    ctx.submit_and_wait(cmd);
    // Compile and warm caches before measuring; recording and upload are excluded.
    std::vector<double> gpu, total;
    for (int i = -3; i < 21; ++i) {
        cmd.mask_bounds(mask, result);
        const auto start = std::chrono::steady_clock::now();
        ctx.submit_and_wait(cmd);
        const auto completed = std::chrono::steady_clock::now();
        const auto bounds = ctx.read(result);
        const auto read = std::chrono::steady_clock::now();
        expect(bounds, Rect{0, 0, 6000, 4000});
        if (i >= 0) {
            gpu.push_back(std::chrono::duration<double, std::milli>(completed - start).count());
            total.push_back(std::chrono::duration<double, std::milli>(read - start).count());
        }
    }
    std::ranges::sort(gpu);
    std::ranges::sort(total);
    std::cout << "24 MP mask_bounds, 21 warm samples, milliseconds: submit+wait median "
              << gpu[10] << " (min " << gpu.front() << ", max " << gpu.back()
              << "); including read median " << total[10] << " (min " << total.front()
              << ", max " << total.back() << ")\n";
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--benchmark") {
        test::run("24 MP mask bounds benchmark", benchmark);
        return test::finish();
    }
    test::run("mask bounds: empty, full, corners, narrow masks and packed tails", [] {
        auto ctx = Context::create();
        auto result = ctx.create_mask_bounds_buffer();
        auto cmd = ctx.create_commands();
        for (const auto size : {ImageSize{1, 1}, {1, 257}, {257, 1}, {3, 7}, {63, 65},
                                {64, 64}, {65, 65}, {257, 4097}}) {
            auto mask = ctx.create_mask(size);
            std::vector<std::uint8_t> pixels(size.width * size.height);
            check(ctx, cmd, mask, result, pixels); // Newly created masks are empty.
            for (const auto index : {std::int64_t{0}, size.width - 1,
                                     size.width * (size.height - 1),
                                     size.width * size.height - 1}) {
                pixels[index] = 1; // Smallest possible nonzero coverage must qualify.
                analysis::upload(ctx, mask, pixels);
                check(ctx, cmd, mask, result, pixels);
                pixels[index] = 0;
            }
            std::ranges::fill(pixels, 255);
            analysis::upload(ctx, mask, pixels);
            check(ctx, cmd, mask, result, pixels);
            std::ranges::fill(pixels, 0);
            analysis::upload(ctx, mask, pixels);
            check(ctx, cmd, mask, result, pixels); // Replaces a previous nonempty result.
            ctx.destroy(mask);
        }
    });
    test::run("mask bounds: all inclusive A8 thresholds and clipping", [] {
        auto ctx = Context::create();
        auto result = ctx.create_mask_bounds_buffer();
        auto cmd = ctx.create_commands();
        auto mask = ctx.create_mask({256, 1});
        std::vector<std::uint8_t> pixels(256);
        for (int i = 0; i < 256; ++i) pixels[i] = i;
        analysis::upload(ctx, mask, pixels);
        for (unsigned threshold = 0; threshold <= 255; ++threshold) {
            check(ctx, cmd, mask, result, pixels, {.threshold = threshold});
            check(ctx, cmd, mask, result, pixels,
                  {.threshold = threshold, .region = Rect{-11, -1, 140, 3}});
        }
    });
    test::run("mask bounds: random masks, regions and thresholds against brute force", [] {
        auto ctx = Context::create();
        auto result = ctx.create_mask_bounds_buffer();
        auto cmd = ctx.create_commands();
        std::mt19937 random(81723);
        for (int trial = 0; trial < 30; ++trial) {
            const int width = 5 + random() % 289, height = 5 + random() % 151;
            auto mask = ctx.create_mask({width, height});
            std::vector<std::uint8_t> pixels(width * height);
            // Sparse data with a varying empty border avoids trivially full bounds.
            const int border = trial % 3;
            for (int y = border; y < height - border; ++y) {
                for (int x = border; x < width - border; ++x) {
                    if (random() % 7 == 0) pixels[y * width + x] = random() % 256;
                }
            }
            analysis::upload(ctx, mask, pixels);
            check(ctx, cmd, mask, result, pixels);
            const int lo = std::numeric_limits<int>::min(), hi = std::numeric_limits<int>::max();
            const std::array regions{Rect{-3, -7, width, height}, Rect{2, 1, width, height},
                                     Rect{width - 1, height - 1, hi, hi}, Rect{lo, lo, hi, hi},
                                     Rect{hi, hi, hi, hi}, Rect{0, 0, 0, height},
                                     Rect{0, 0, width, 0}, Rect{width / 2, 0, 1, height},
                                     Rect{0, height / 2, width, 1},
                                     Rect{int(random() % width), int(random() % height),
                                          int(random() % width), int(random() % height)}};
            for (const auto region : regions) {
                for (const unsigned threshold : {0u, 1u, 127u, 128u, 254u, 255u}) {
                    check(ctx, cmd, mask, result, pixels, {.threshold = threshold, .region = region});
                }
            }
            ctx.destroy(mask);
        }
    });
    test::run("mask bounds: 24 MP sparse, random, full and empty reductions", [] {
        constexpr int width = 6000, height = 4000;
        auto ctx = Context::create();
        auto mask = ctx.create_mask({width, height});
        auto result = ctx.create_mask_bounds_buffer();
        auto cmd = ctx.create_commands();
        std::vector<std::uint8_t> pixels(width * height);
        for (const int index : {0, width - 1, width * (height - 1), width * height - 1}) {
            pixels[index] = 1;
            analysis::upload(ctx, mask, pixels);
            check(ctx, cmd, mask, result, pixels);
            pixels[index] = 0;
        }
        // Hits straddle group and dispatch boundaries, including the final partial chunk.
        for (std::size_t i : {0u, 4095u, 4096u, 1048575u, 1048576u, 23999999u}) pixels[i] = 255;
        analysis::upload(ctx, mask, pixels);
        check(ctx, cmd, mask, result, pixels);
        // Only an early hit: every subsequent empty chunk must preserve the result.
        std::ranges::fill(pixels, 0);
        pixels[17] = 1;
        analysis::upload(ctx, mask, pixels);
        check(ctx, cmd, mask, result, pixels);
        // Random island spanning many chunks, with clipped regions and thresholded coverage.
        std::mt19937 random(271828);
        for (int y = 37; y < height - 19; ++y) {
            for (int x = 53; x < width - 41; ++x) {
                if (random() % 97 == 0) pixels[y * width + x] = 1 + random() % 255;
            }
        }
        analysis::upload(ctx, mask, pixels);
        for (unsigned threshold : {1u, 128u, 255u}) {
            check(ctx, cmd, mask, result, pixels, {.threshold = threshold});
            check(ctx, cmd, mask, result, pixels,
                  {.threshold = threshold, .region = Rect{49, -13, 5803, 3951}});
        }
        std::ranges::fill(pixels, 255);
        analysis::upload(ctx, mask, pixels);
        check(ctx, cmd, mask, result, pixels);
        std::ranges::fill(pixels, 0);
        analysis::upload(ctx, mask, pixels);
        check(ctx, cmd, mask, result, pixels);
    });
    test::run("mask bounds: result lifetime, async retirement, ordering and captured dimensions", [] {
        auto ctx = Context::create();
        auto mask = ctx.create_mask({7, 5});
        const auto before = ctx.memory().internal;
        auto result = ctx.create_mask_bounds_buffer();
        test::check(ctx.memory().internal - before == 32, "bounds storage must be constant 32 bytes");
        auto alias = result;
        test::error(ErrorCode::invalid_argument, "read", "buffer", [&] { (void)ctx.read(result); });
        auto cmd = ctx.create_commands();
        cmd.fill(mask, {.coverage = 1, .region = Rect{3, 1, 2, 3}});
        cmd.mask_bounds(mask, result);
        test::error(ErrorCode::resource_busy, "read", "buffer", [&] { (void)ctx.read(result); });
        test::error(ErrorCode::resource_busy, "destroy", "resource", [&] { ctx.destroy(alias); });
        test::error(ErrorCode::resource_busy, "destroy", "resource", [&] { ctx.destroy(mask); });
        mask.set_size({5, 7}); // Recorded coordinates still use the 7x5 layout.
        auto done = ctx.submit(cmd);
        test::error(ErrorCode::resource_busy, "read", "buffer", [&] { (void)ctx.read(result); });
        test::error(ErrorCode::resource_busy, "destroy", "resource", [&] { ctx.destroy(alias); });
        while (!ctx.is_complete(done)) std::this_thread::yield();
        expect(ctx.read(alias), Rect{3, 1, 2, 3});
        expect(ctx.read(result), Rect{3, 1, 2, 3}); // Repeated reads are valid.
        mask.set_size({7, 5});
        {
            auto discarded = ctx.create_commands();
            discarded.mask_bounds(mask, result, {.threshold = 0});
        }
        expect(ctx.read(result), Rect{3, 1, 2, 3});
        auto earlier = ctx.create_mask_bounds_buffer();
        cmd.mask_bounds(mask, earlier);
        cmd.fill(mask, {.coverage = 0});
        cmd.mask_bounds(mask, result); // Two writes to one result replace, never union.
        cmd.fill(mask, {.coverage = 1, .region = Rect{6, 4, 1, 1}});
        cmd.mask_bounds(mask, result);
        done = ctx.submit(cmd);
        mask = {}; // Flight owns the source until retirement.
        ctx.wait(done);
        expect(ctx.read(earlier), Rect{3, 1, 2, 3});
        expect(ctx.read(result), Rect{6, 4, 1, 1});
        ctx.destroy(result);
        test::error(ErrorCode::invalid_resource, "read", "buffer", [&] { (void)ctx.read(alias); });
    });
    test::run("mask bounds: measures a magic wand recorded in the same commands", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({33, 17});
        auto mask = ctx.create_mask(image.size());
        auto result = ctx.create_mask_bounds_buffer();
        MagicWandOptions wand{.seed = {6, 5}, .tolerance = 0, .anti_alias = false};
        wand.workspace =
            ctx.create_workspace(select_magic_wand_requirements(image.size(), wand).workspace);
        auto cmd = ctx.create_commands();
        cmd.fill(image, {.color = {0, 0, 1, 1}});
        cmd.fill(image, {.color = {0, 1, 0, 1}, .region = Rect{4, 3, 9, 7}});
        cmd.fill(image, {.color = {0, 1, 0, 1}, .region = Rect{25, 12, 3, 3}});
        cmd.select_magic_wand(image, mask, wand);
        cmd.mask_bounds(mask, result);
        ctx.submit_and_wait(cmd);
        std::vector<std::uint8_t> expected(33 * 17);
        for (int y = 3; y < 10; ++y) {
            for (int x = 4; x < 13; ++x) expected[y * 33 + x] = 255;
        }
        expect(ctx.read(result), reference(expected, 33));
    });
    test::run("mask bounds: invalid arguments leave earlier work and results intact", [] {
        auto ctx = Context::create();
        auto other = Context::create();
        auto mask = ctx.create_mask({7, 5});
        auto result = ctx.create_mask_bounds_buffer();
        auto foreign_mask = other.create_mask({7, 5});
        auto foreign_result = other.create_mask_bounds_buffer();
        auto cmd = ctx.create_commands();
        cmd.mask_bounds(mask, result);
        ctx.submit_and_wait(cmd);
        cmd.fill(mask, {.coverage = 1});
        for (const unsigned threshold : {256u, std::numeric_limits<unsigned>::max()}) {
            test::error(ErrorCode::invalid_argument, "mask_bounds", "threshold", [&] {
                cmd.mask_bounds(mask, result, {.threshold = threshold});
            });
        }
        for (const auto region : {Rect{0, 0, -1, 2}, Rect{0, 0, 2, -1}}) {
            test::error(ErrorCode::invalid_argument, "mask_bounds", "region", [&] {
                cmd.mask_bounds(mask, result, {.region = region});
            });
        }
        for (const auto& source : {Mask{}, foreign_mask}) {
            test::error(ErrorCode::invalid_resource, "mask_bounds", "source", [&] {
                cmd.mask_bounds(source, result);
            });
        }
        for (const auto& destination : {MaskBoundsBuffer{}, foreign_result}) {
            test::error(ErrorCode::invalid_resource, "mask_bounds", "destination", [&] {
                cmd.mask_bounds(mask, destination);
            });
        }
        test::error(ErrorCode::invalid_resource, "read", "buffer",
                    [&] { (void)ctx.read(foreign_result); });
        test::error(ErrorCode::invalid_resource, "destroy", "resource",
                    [&] { ctx.destroy(foreign_result); });
        expect(ctx.read(result), std::nullopt); // Failed calls left no result records behind.
        cmd.mask_bounds(mask, result);
        ctx.submit_and_wait(cmd);
        expect(ctx.read(result), Rect{0, 0, 7, 5}); // Earlier fill survived.
        ctx.destroy(mask);
        test::error(ErrorCode::invalid_resource, "mask_bounds", "source",
                    [&] { cmd.mask_bounds(mask, result); });
    });
    test::run("mask bounds: failed capacity growth rolls back the entire reduction", [] {
        auto ctx = Context::create();
        auto mask = ctx.create_mask({6000, 4000});
        auto result = ctx.create_mask_bounds_buffer();
        auto cmd = ctx.create_commands(1);
        cmd.fill(mask, {.coverage = 1, .region = Rect{6, 4, 1, 1}});
        ctx.set_memory_limit(ctx.memory().total);
        test::error(ErrorCode::capacity, "mask_bounds", "memory_limit",
                    [&] { cmd.mask_bounds(mask, result); });
        ctx.destroy(result); // No partially recorded result references.
        ctx.set_memory_limit(0);
        ctx.submit_and_wait(cmd);
        result = ctx.create_mask_bounds_buffer();
        cmd.mask_bounds(mask, result);
        ctx.submit_and_wait(cmd);
        expect(ctx.read(result), Rect{6, 4, 1, 1});
    });
    return test::finish();
}
