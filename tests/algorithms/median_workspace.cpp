#include "../test.h"

using namespace wgpupixel;

int main() {
    test::run("median workspace preflight leaves earlier commands intact", [] {
        auto ctx = Context::create();
        const ImageSize size{37, 19};
        auto source = ctx.create_image(size), destination = ctx.create_image(size);
        auto readback = ctx.create_readback_buffer(destination, {.format = TransferFormat::rgba32_float});
        auto cmd = ctx.create_commands(4096);
        cmd.fill(destination, {.color = {-2, 3, .25f, 1}});
        const auto baseline = ctx.memory().total;
        test::error(ErrorCode::capacity, "median", "workspace", [&] {
            cmd.median(source, destination, {.radius = 5});
        });
        test::check(ctx.memory().total == baseline, "missing workspace must not allocate");
        auto small = ctx.create_workspace(median_requirements({1, 1}, {.radius = 4}).workspace);
        test::error(ErrorCode::capacity, "median", "workspace", [&] {
            cmd.median(source, destination, {.radius = 5, .workspace = small});
        });
        auto other = Context::create();
        auto foreign = other.create_workspace(median_requirements(size, {.radius = 5}).workspace);
        test::error(ErrorCode::invalid_resource, "median", "workspace", [&] {
            cmd.median(source, destination, {.radius = 5, .workspace = foreign});
        });
        ctx.destroy(small);
        test::error(ErrorCode::invalid_resource, "median", "workspace", [&] {
            cmd.median(source, destination, {.radius = 5, .workspace = small});
        });
        cmd.download(destination, readback);
        ctx.submit_and_wait(cmd);
        std::vector<float> pixels(size.width * size.height * 4);
        ctx.read(readback, {reinterpret_cast<std::uint8_t*>(pixels.data()), pixels.size() * sizeof(float)});
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            test::near(pixels[i], -2);
            test::near(pixels[i + 1], 3);
            test::near(pixels[i + 2], .25f);
            test::near(pixels[i + 3], 1);
        }
    });
    test::run("merged median plans serve consecutive operations without allocation", [] {
        auto ctx = Context::create();
        const ImageSize size{37, 19};
        auto source = ctx.create_image(size), result = ctx.create_image(size);
        auto small = median_requirements(size, {.radius = 5}).workspace;
        auto large = median_requirements(size, {.radius = 10}).workspace;
        small.merge(large);
        auto workspace = ctx.create_workspace(small);
        auto cmd = ctx.create_commands(16384);
        auto readback = ctx.create_readback_buffer(result, {.format = TransferFormat::rgba32_float});
        cmd.fill(source, {.color = {-2, 3, .25f, 1}});
        const auto before = ctx.memory();
        cmd.median(source, result, {.radius = 10, .workspace = workspace});
        cmd.median(result, source, {.radius = 5, .workspace = workspace});
        cmd.median(source, result, {.radius = 10, .workspace = workspace});
        test::check(ctx.memory().workspace == before.workspace, "views must not double count buffers");
        test::check(ctx.memory().total == before.total, "recording must not allocate GPU storage");
        cmd.download(result, readback);
        ctx.submit_and_wait(cmd);
        std::vector<float> pixels(size.width * size.height * 4);
        ctx.read(readback, {reinterpret_cast<std::uint8_t*>(pixels.data()), pixels.size() * sizeof(float)});
        // A constant image is invariant under every component-wise median radius.
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            test::near(pixels[i], -2);
            test::near(pixels[i + 1], 3);
            test::near(pixels[i + 2], .25f);
            test::near(pixels[i + 3], 1);
        }
        ctx.destroy(workspace);
        test::check(ctx.memory().workspace == 0, "retired views release their backing owners");
    });
    test::run("recordings and submissions retain dropped workspace handles", [] {
        auto ctx = Context::create();
        const ImageSize size{37, 19};
        auto source = ctx.create_image(size), result = ctx.create_image(size);
        const auto plan = median_requirements(size, {.radius = 5}).workspace;
        {
            auto workspace = ctx.create_workspace(plan);
            auto cmd = ctx.create_commands();
            cmd.median(source, result, {.radius = 5, .workspace = workspace});
            workspace = {};
            test::check(ctx.memory().workspace == plan.bytes(), "recording retains dropped workspace");
        }
        test::check(ctx.memory().workspace == 0, "abandoned commands release workspace");
        auto workspace = ctx.create_workspace(plan);
        auto cmd = ctx.create_commands();
        cmd.fill(source, {.color = {.25f, .5f, .75f, 1}});
        cmd.median(source, result, {.radius = 5, .workspace = workspace});
        auto done = ctx.submit(cmd);
        test::error(ErrorCode::resource_busy, "destroy", "workspace", [&] { ctx.destroy(workspace); });
        workspace = {};
        cmd = {};
        test::check(ctx.memory().workspace == plan.bytes(), "pending work retains dropped workspace");
        ctx.wait(done);
        test::check(ctx.memory().workspace == 0, "retirement releases the last backing owner");
        const auto pixels = test::read(ctx, result);
        const auto expected = test::encode(std::array<float, 4>{.25f, .5f, .75f, 1});
        for (std::size_t i = 0; i < pixels.size(); ++i)
            test::check(pixels[i] == expected[i % 4], "retained workspace produces valid pixels");
    });
    return test::finish();
}
