#include "test.h"
#include <array>
#include <utility>

using namespace wgpupixel;

namespace {

void reuse_and_old_submissions(Context& ctx) {
    auto image = ctx.create_image({1, 1});
    auto output = ctx.create_readback_buffer(image);
    auto cmd = ctx.create_commands(2);
    std::array<std::uint8_t, 4> pixels{};

    cmd.fill(image, {.color = {1, 0, 0, 1}});
    cmd.download(image, output);
    auto first = ctx.submit(cmd);
    auto first_copy = first;
    ctx.wait(first);

    cmd.fill(image, {.color = {0, 1, 0, 1}});
    cmd.download(image, output);
    auto second = ctx.submit(cmd);
    ctx.wait(first_copy);
    test::error(ErrorCode::resource_busy, "fill", "commands",
                [&] { cmd.fill(image, {.color = {0, 0, 1, 1}}); });
    test::error(ErrorCode::resource_busy, "submit", "commands", [&] { (void)ctx.submit(cmd); });
    ctx.wait(second);
    ctx.read(output, pixels);
    test::near(pixels[0], 0);
    test::near(pixels[1], 255);

    // Submitting again without recording must not replay the previous download.
    ctx.destroy(output);
    ctx.destroy(image);
    ctx.submit_and_wait(cmd);
    ctx.wait(first);
    ctx.wait(second);
}

void empty_and_zero_radius(Context& ctx) {
    auto image = ctx.create_image({1, 1});
    auto temp = ctx.create_image({1, 1});
    auto cmd = ctx.create_commands(1);
    ctx.submit_and_wait(cmd);
    cmd.gaussian_blur(image, {.radius = 0, .sigma = 1});
    ctx.submit_and_wait(cmd);
    cmd.gaussian_blur(image, {.radius = 0, .sigma = 1});
    cmd.fill(image, {.color = {0.25f, 0.5f, 0.75f, 1}});
    ctx.submit_and_wait(cmd);
    cmd.gaussian_blur(image, {.radius = 0, .sigma = 1});
    ctx.destroy(temp);
    ctx.submit_and_wait(cmd);
    auto pixels = test::read(ctx, image);
    test::near(pixels[0], 137);
    test::near(pixels[2], 225);
    ctx.destroy(image);
}

void queued_submissions(Context& ctx) {
    auto image = ctx.create_image({1, 1});
    auto first_commands = ctx.create_commands(1);
    auto second_commands = ctx.create_commands(1);
    first_commands.fill(image, {.color = {0.25f, 0, 0, 1}});
    auto first = ctx.submit(first_commands);
    second_commands.brightness(image, {.amount = 0.5f});
    auto second = ctx.submit(second_commands);
    first_commands = Commands{};
    second_commands = Commands{};
    ctx.wait(second);
    ctx.wait(first);
    auto pixels = test::read(ctx, image);
    test::near(pixels[0], 225);
    test::near(pixels[1], 188);
    ctx.destroy(image);
}

void busy_transfers(Context& ctx) {
    auto image = ctx.create_image({1, 1});
    auto input = ctx.create_upload_buffer(image);
    auto output = ctx.create_readback_buffer(image);
    auto cmd = ctx.create_commands(2);
    std::array<std::uint8_t, 4> pixels{51, 102, 153, 255};
    ctx.write(input, pixels);
    cmd.upload(input, image);
    cmd.download(image, output);

    auto check_busy = [&] {
        test::error(ErrorCode::resource_busy, "write", "buffer", [&] { ctx.write(input, pixels); });
        test::error(ErrorCode::resource_busy, "read", "buffer", [&] { ctx.read(output, pixels); });
        test::error(ErrorCode::resource_busy, "destroy", "resource", [&] { ctx.destroy(image); });
        test::error(ErrorCode::resource_busy, "destroy", "resource", [&] { ctx.destroy(input); });
        test::error(ErrorCode::resource_busy, "destroy", "resource", [&] { ctx.destroy(output); });
    };
    check_busy();
    auto done = ctx.submit(cmd);
    check_busy();
    ctx.wait(done);
    ctx.read(output, pixels);
    test::near(pixels[0], 51);
    test::near(pixels[3], 255);
    ctx.write(input, pixels);
    ctx.destroy(input);
    ctx.destroy(output);
    ctx.destroy(image);
}

void command_moves_and_destruction(Context& ctx) {
    auto image = ctx.create_image({1, 1});
    auto discarded = ctx.create_image({1, 1});
    auto cmd = ctx.create_commands(1);
    cmd.fill(image, {.color = {1, 0, 0, 1}});
    auto moved = std::move(cmd);
    test::error(ErrorCode::invalid_resource, "fill", "commands",
                [&] { cmd.fill(image, {.color = {0, 0, 0, 1}}); });
    cmd = ctx.create_commands(1);
    cmd.fill(discarded, {.color = {0, 1, 0, 1}});
    cmd = std::move(moved);
    ctx.destroy(discarded);

    auto done = ctx.submit(cmd);
    moved = std::move(cmd);
    test::error(ErrorCode::resource_busy, "fill", "commands",
                [&] { moved.fill(image, {.color = {0, 0, 0, 1}}); });
    moved = Commands{};
    ctx.wait(done);
    test::near(test::read(ctx, image)[0], 255);
    ctx.destroy(image);

    auto abandoned = ctx.create_image({1, 1});
    {
        auto recorded = ctx.create_commands(1);
        recorded.fill(abandoned, {.color = {1, 1, 1, 1}});
    }
    ctx.destroy(abandoned);
}

void aliases_and_size_snapshots(Context& ctx) {
    auto image = ctx.create_image({4, 2});
    auto alias = image;
    auto input = ctx.create_upload_buffer(image);
    auto input_alias = input;
    auto output = ctx.create_readback_buffer(image);
    auto output_alias = output;
    std::array<std::uint8_t, 32> pixels{};
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = static_cast<std::uint8_t>(i * 7);
    }
    ctx.write(input, pixels);
    auto cmd = ctx.create_commands(3);
    cmd.upload(input, image);
    alias.set_size({2, 1});
    test::check(image.size().width == 2 && image.size().height == 1, "aliases share dimensions");
    test::check(alias.capacity_pixels() == 8, "set_size preserves capacity");
    cmd.fill(alias, {.color = {1, 0, 0, 1}});
    image.set_size({4, 2});
    cmd.download(image, output);
    image.set_size({1, 1});
    ctx.submit_and_wait(cmd);
    std::array<std::uint8_t, 32> result{};
    ctx.read(output_alias, result);
    for (std::size_t i = 0; i < 8; ++i) {
        test::near(result[i], i % 4 == 0 || i % 4 == 3 ? 255.0f : 0.0f);
    }
    for (std::size_t i = 8; i < result.size(); ++i) {
        test::near(result[i], pixels[i]);
    }
    ctx.destroy(image);
    ctx.destroy(input);
    ctx.destroy(output);
    test::error(ErrorCode::invalid_resource, "size", "image", [&] { (void)alias.size().width; });
    test::error(ErrorCode::invalid_resource, "capacity_pixels", "resource",
                [&] { (void)input_alias.capacity_pixels(); });
    test::error(ErrorCode::invalid_resource, "capacity_pixels", "resource",
                [&] { (void)output_alias.capacity_pixels(); });
}

void repeated_buffer_reuse(Context& ctx) {
    auto image = ctx.create_image({9, 7});
    auto input = ctx.create_upload_buffer(image);
    auto output = ctx.create_readback_buffer(image);
    auto cmd = ctx.create_commands(3);
    std::array<std::uint8_t, 9 * 7 * 4> pixels{}, result{};
    for (int iteration = 0; iteration < 1000; ++iteration) {
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            pixels[i] = static_cast<std::uint8_t>((i + static_cast<std::size_t>(iteration)) % 256);
        }
        ctx.write(input, pixels);
        cmd.upload(input, image);
        const float amount = iteration % 2 == 0 ? 0.125f : -0.25f;
        cmd.brightness(image, {.amount = amount});
        cmd.download(image, output);
        auto done = ctx.submit(cmd);
        ctx.wait(done);
        ctx.read(output, result);
        for (std::size_t i = 0; i < result.size(); ++i) {
            const float alpha = pixels[(i / 4) * 4 + 3];
            const double encoded = pixels[i] / 255.0;
            const float linear = static_cast<float>(
                encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4));
            const float expected =
                alpha == 0 ? 0 : (i % 4 == 3 ? alpha : test::channel(linear + amount));
            test::near(result[i], expected, 1);
        }
    }
    ctx.destroy(output);
    ctx.destroy(input);
    ctx.destroy(image);
}

void repeated_resource_creation(Context& ctx) {
    for (int iteration = 0; iteration < 100; ++iteration) {
        auto image = ctx.create_image({3, 2});
        auto input = ctx.create_upload_buffer(image);
        auto output = ctx.create_readback_buffer(image);
        auto cmd = ctx.create_commands(2);
        cmd.fill(image, {.color = {0.5f, 0, 0, 1}});
        cmd.download(image, output);
        auto done = ctx.submit(cmd);
        cmd = Commands{};
        ctx.wait(done);
        std::array<std::uint8_t, 24> pixels{};
        ctx.read(output, pixels);
        test::near(pixels[0], 188);
        test::near(pixels[23], 255);
        ctx.destroy(image);
        ctx.destroy(input);
        ctx.destroy(output);
    }
}

void last_owner_releases(Context& ctx) {
    const auto baseline = ctx.memory().total;
    ctx.set_memory_limit(baseline + (32ull << 20));
    for (int i = 0; i < 10; ++i) {
        auto image = ctx.create_image({1024, 1024});
        auto alias = image;
        image = {};
        test::check(ctx.memory().total == baseline + (16ull << 20),
                    "a live alias must retain its allocation");
    }
    test::check(ctx.memory().total == baseline, "last image handle must release storage");
    ctx.set_memory_limit(0);
    {
        auto image = ctx.create_image({17, 19});
        auto mask = ctx.create_mask({17, 19});
        auto upload = ctx.create_upload_buffer(image);
        auto readback = ctx.create_readback_buffer(mask);
        auto histogram = ctx.create_histogram_buffer(256);
        auto statistics = ctx.create_statistics_buffer();
        auto bounds = ctx.create_mask_bounds_buffer();
        test::check(ctx.memory().total > baseline, "resource fixture must allocate storage");
    }
    test::check(ctx.memory().total == baseline, "all public resource kinds release on scope exit");
    try {
        auto image = ctx.create_image({17, 19});
        throw 1;
    } catch (int) {
    }
    test::check(ctx.memory().total == baseline, "exception unwinding must release storage");
}

void recording_owns_resources(Context& ctx) {
    const auto baseline = ctx.memory().images;
    auto commands = ctx.create_commands(2);
    auto image = ctx.create_image({17, 19});
    auto readback = ctx.create_readback_buffer(image);
    commands.fill(image, {.color = {.25f, .5f, .75f, 1}});
    commands.download(image, readback);
    image = {};
    test::check(ctx.memory().images == baseline + 17 * 19 * 16,
                "recorded commands must retain dropped handles");
    auto done = ctx.submit(commands);
    commands = {};
    test::check(ctx.memory().images == baseline + 17 * 19 * 16,
                "pending submission must retain dropped handles and commands");
    ctx.wait(done);
    test::check(ctx.memory().images == baseline,
                "retirement must release images even while Submission survives");
    std::array<std::uint8_t, 17 * 19 * 4> pixels{};
    ctx.read(readback, pixels);
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        test::near(pixels[i], 137);
        test::near(pixels[i + 1], 188);
        test::near(pixels[i + 2], 225);
        test::near(pixels[i + 3], 255);
    }
    {
        auto discarded = ctx.create_commands();
        auto temporary = ctx.create_image({17, 19});
        discarded.fill(temporary, {.color = {1, 0, 0, 1}});
        temporary = {};
        test::check(ctx.memory().images > baseline, "abandoned recording retains its image");
    }
    test::check(ctx.memory().images == baseline, "discarded recording must release its image");
}

void context_lifetimes() {
    for (int iteration = 0; iteration < 4; ++iteration) {
        Image image;
        Commands commands;
        Submission submitted;
        {
            auto original = Context::create();
            image = original.create_image({1, 1});
            commands = original.create_commands(1);
            auto moved = std::move(original);
            test::error(ErrorCode::invalid_resource, "create_image", "context",
                        [&] { (void)original.create_image({1, 1}); });
            original = std::move(moved);
            commands.fill(image, {.color = {0.5f, 0, 0, 1}});
            submitted = original.submit(commands);
            if (iteration % 2 == 0) {
                original.wait(submitted);
            }
        }
        test::check(image.size().width == 1, "commands keep their context alive");
        commands = Commands{};
        test::error(ErrorCode::invalid_resource, "size", "resource",
                    [&] { (void)image.size().width; });
        // A surviving submission must not keep a destroyed context alive.
        submitted = Submission{};
    }
}

} // namespace

int main() {
    test::run("lifetime context", [] {
        auto ctx = Context::create();
        test::run("reuse and old submissions", [&] { reuse_and_old_submissions(ctx); });
        test::run("empty and zero-radius submissions", [&] { empty_and_zero_radius(ctx); });
        test::run("queued submissions share resources", [&] { queued_submissions(ctx); });
        test::run("recorded and pending transfers", [&] { busy_transfers(ctx); });
        test::run("command moves and destruction", [&] { command_moves_and_destruction(ctx); });
        test::run("aliases and dimension snapshots", [&] { aliases_and_size_snapshots(ctx); });
        test::run("1000 buffer reuse iterations", [&] { repeated_buffer_reuse(ctx); });
        test::run("100 resource creation cycles", [&] { repeated_resource_creation(ctx); });
        test::run("last owners release every public resource kind", [&] { last_owner_releases(ctx); });
        test::run("recordings and submissions retain dropped handles", [&] { recording_owns_resources(ctx); });
    });
    test::run("context moves and destruction", context_lifetimes);
    return test::finish();
}
