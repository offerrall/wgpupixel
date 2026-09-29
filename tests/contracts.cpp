#include "test.h"

#include <array>
#include <limits>
#include <type_traits>
#include <utility>

using namespace wgpupixel;
using enum ErrorCode;

// Options and sizes are plain aggregates: designated initializers, copies and assignment.
template <class... T> constexpr bool plain_values = ((std::is_aggregate_v<T> &&
    std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>) && ...);
static_assert(plain_values<ImageSize, ResourceLimits, OperationRequirements, DropShadowOptions,
                           CopyOptions, BrushStrokeOptions, BlendManyOptions, TransferOptions,
                           TransferBufferOptions, Point, Affine, GradientStop>);
static_assert(std::is_assignable_v<decltype((std::declval<DropShadowOptions&>().radius)), int>);
static_assert(std::is_assignable_v<decltype((std::declval<CopyOptions&>().mask)), const Mask*>);
static_assert([] {
    FillOptions options{.color = {1, 0, 0, 1}};
    options.region = Rect{0, 0, 2, 2};
    ImageSize size{4, 2};
    size = {8, 4};
    return options.region->width == 2 && size == ImageSize{8, 4};
}());
static_assert(
    !std::is_invocable_v<decltype(&Commands::brightness), Commands&, const Image&, float>);
static_assert(std::is_invocable_v<decltype(&Commands::brightness), Commands&, const Image&,
                                  const BrightnessOptions&>);

template <class Function>
concept CanRun =
    requires(Context& ctx, Function&& record) { ctx.run_and_wait(std::forward<Function>(record)); };
static_assert(CanRun<decltype([](Commands&) {})>);
static_assert(!CanRun<decltype([](Commands&) { return 1; })>);
static_assert(!CanRun<decltype([]() {})>);

int main() {
    test::run("run_and_wait records once and returns completed pixels", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        int calls = 0;
        auto record = [&, owned = std::make_unique<int>(1)](Commands& cmd) {
            calls += *owned;
            cmd.fill(image, {.color = {1, 0, 0, 1}});
            cmd.invert(image);
        };
        ctx.run_and_wait(std::move(record));
        test::check(calls == 1, "callback must run exactly once");
        test::check(test::read(ctx, image) == std::vector<std::uint8_t>{0, 255, 255, 255},
                    "batch must finish before returning");
        ctx.destroy(image);
    });

    test::run("fill rejects colors outside the Color contract and keeps earlier work", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        auto cmd = ctx.create_commands();
        cmd.fill(image, {.color = {1, 0, 0, 1}});
        const float nan = std::numeric_limits<float>::quiet_NaN();
        for (Color color : {Color{0, 1, 0, 2}, Color{0, 1, 0, -0.25f}, Color{0, 1, 0, nan},
                            Color{nan, 1, 0, 1}}) {
            test::error(invalid_argument, "fill", "color",
                        [&] { cmd.fill(image, {.color = color}); });
        }
        test::check(image.revision() == 0, "rejected fills must not change the revision");
        ctx.submit_and_wait(cmd);
        test::check(test::read(ctx, image) == std::vector<std::uint8_t>{255, 0, 0, 255},
                    "earlier fill must survive rejected colors");
        ctx.destroy(image);
    });

    test::run("run_and_wait discards failed recordings and releases their references", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1, 1});
        ctx.run_and_wait([&](Commands& cmd) { cmd.fill(image, {.color = {1, 0, 0, 1}}); });
        const auto revision = image.revision();
        bool caught = false;
        try {
            ctx.run_and_wait([&](Commands& cmd) {
                cmd.fill(image, {.color = {0, 1, 0, 1}});
                throw std::runtime_error("recording failed");
            });
        } catch (const std::runtime_error& error) {
            caught = std::string_view(error.what()) == "recording failed";
        }
        test::check(caught && image.revision() == revision, "preserve exception and revision");
        test::check(test::read(ctx, image) == std::vector<std::uint8_t>{255, 0, 0, 255},
                    "failed callbacks must not send partially recorded work");
        ctx.destroy(image); // Failed recordings must not leave the resource busy.
        Context empty;
        bool called = false;
        test::error(invalid_resource, "create_commands", "context",
                    [&] { empty.run_and_wait([&](Commands&) { called = true; }); });
        test::check(!called, "invalid context must fail before invoking the callback");
    });

    test::run("default handles and stable error metadata", [] {
        Context empty;
        Image image;
        UploadBuffer upload;
        ReadbackBuffer readback;
        Commands commands;
        test::error(invalid_resource, "create_image", "context",
                    [&] { (void)empty.create_image({1, 1}); });
        test::error(invalid_resource, "prepare", "context", [&] { empty.prepare(); });
        test::error(invalid_resource, "size", "resource", [&] { (void)image.size().width; });
        test::error(invalid_resource, "size", "resource", [&] { (void)image.size().height; });
        test::error(invalid_resource, "capacity_pixels", "resource",
                    [&] { (void)upload.capacity_pixels(); });
        test::error(invalid_resource, "capacity_pixels", "resource",
                    [&] { (void)readback.capacity_pixels(); });
        test::error(invalid_resource, "fill", "commands",
                    [&] { commands.fill(image, {.color = {0, 0, 0, 1}}); });
        const Error original(capacity, "operation", "parameter", "message");
        const auto copy = original;
        test::check(copy.code() == capacity && copy.operation() == "operation" &&
                        copy.parameter() == "parameter" &&
                        std::string_view(copy.what()) == "message",
                    "error copy must preserve metadata");
        const std::string long_text(2048, 'x');
        const Error truncated(invalid_argument, long_text, long_text, long_text);
        test::check(truncated.operation().size() == 63 && truncated.parameter().size() == 63 &&
                        std::string_view(truncated.what()).size() == 1023,
                    "oversized metadata must be terminated and bounded");
    });

    test::run("pipelines compile on first use and prepare is idempotent", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({2, 2});
        auto other = ctx.create_image({2, 2});
        auto cmd = ctx.create_commands(2);
        cmd.copy(image, other);
        ctx.submit_and_wait(cmd); // A buffer copy needs no processing pipeline.
        cmd.fill(image, {.color = {1, 0, 0, 1}});
        cmd.copy(image, other);
        ctx.submit_and_wait(cmd); // Compiles fill without prepare.
        ctx.prepare();
        ctx.prepare();
        ctx.submit_and_wait(cmd);
        test::check(test::read(ctx, other) ==
                        std::vector<std::uint8_t>(
                            {255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255}),
                    "preparation must preserve resources and recorded commands");
        ctx.destroy(image);
        ctx.destroy(other);
    });

    test::run("GPU validation contracts", [] {
        auto ctx = Context::create();

        test::run("impossible dimensions and capacities leave size unchanged", [&] {
            for (const auto width :
                 {std::int64_t{0}, std::int64_t{-1}, std::numeric_limits<std::int64_t>::max()}) {
                test::error(invalid_argument, "create_image", "size.width",
                            [&] { (void)ctx.create_image({width, 1}); });
            }
            test::error(invalid_argument, "create_image", "size.height",
                        [&] { (void)ctx.create_image({1, 0}); });
            test::error(capacity, "create_image", "size",
                        [&] { (void)ctx.create_image({65536, 65536}); });
            (void)ctx.create_commands(0);
            test::error(capacity, "create_commands", "command_capacity", [&] {
                (void)ctx.create_commands(std::numeric_limits<std::size_t>::max());
            });
            auto image = ctx.create_image({2, 2});
            test::error(capacity, "set_size", "size", [&] { image.set_size({3, 2}); });
            test::error(invalid_argument, "set_size", "size.height", [&] { image.set_size({2, -1}); });
            test::check(image.size().width == 2 && image.size().height == 2 &&
                            image.capacity_pixels() == 4,
                        "failed size changes must not alter dimensions or capacity");
            image.set_size({1, 4});
            test::check(image.size().width == 1 && image.size().height == 4 &&
                            image.capacity_pixels() == 4,
                        "logical size may change shape within capacity");
            ctx.destroy(image);
        });

        test::run("default, moved and destroyed resources", [&] {
            auto cmd = ctx.create_commands();
            test::error(invalid_resource, "fill", "destination",
                        [&] { cmd.fill(Image{}, {.color = {0, 0, 0, 1}}); });
            test::error(invalid_resource, "create_upload_buffer", "image",
                        [&] { (void)ctx.create_upload_buffer(Image{}); });
            auto selection = ctx.create_mask({1, 1});
            test::error(invalid_resource, "fill", "destination", [&] {
                cmd.fill(Image{}, {.color = {0, 0, 0, 1}, .mask = &selection});
            });
            auto histogram = ctx.create_histogram_buffer();
            test::error(invalid_resource, "histogram", "source", [&] {
                cmd.histogram(Image{}, histogram, {.mask = &selection});
            });
            ctx.destroy(histogram);
            ctx.destroy(selection);
            Commands empty;
            test::error(invalid_resource, "submit", "commands", [&] { (void)ctx.submit(empty); });
            test::error(invalid_resource, "wait", "submission", [&] { ctx.wait(Submission{}); });
            auto moved = std::move(cmd);
            test::error(invalid_resource, "grayscale", "commands", [&] { cmd.grayscale(Image{}); });
            auto image = ctx.create_image({1, 1});
            const auto alias = image;
            auto upload = ctx.create_upload_buffer(image);
            auto readback = ctx.create_readback_buffer(image);
            ctx.destroy(image);
            test::error(invalid_resource, "size", "image", [&] { (void)alias.size().width; });
            test::error(invalid_resource, "grayscale", "destination",
                        [&] { moved.grayscale(alias); });
            test::error(invalid_resource, "destroy", "resource", [&] { ctx.destroy(image); });
            ctx.destroy(upload);
            ctx.destroy(readback);
            std::array<std::uint8_t, 4> pixel{};
            test::error(invalid_resource, "write", "buffer", [&] { ctx.write(upload, pixel); });
            test::error(invalid_resource, "read", "buffer", [&] { ctx.read(readback, pixel); });
            auto destination = std::move(image);
            test::error(invalid_resource, "size", "resource", [&] { (void)image.size().height; });
            test::error(invalid_resource, "size", "image",
                        [&] { (void)destination.size().height; });
        });

        test::run("foreign contexts and moved context", [&] {
            auto other = Context::create();
            auto local = ctx.create_image({1, 1});
            auto foreign = other.create_image({1, 1});
            auto upload = other.create_upload_buffer(foreign);
            auto readback = other.create_readback_buffer(foreign);
            auto cmd = ctx.create_commands();
            auto foreign_cmd = other.create_commands();
            test::error(invalid_resource, "copy", "source", [&] { cmd.copy(foreign, local); });
            test::error(invalid_resource, "copy", "destination", [&] { cmd.copy(local, foreign); });
            test::error(invalid_resource, "upload", "source", [&] { cmd.upload(upload, local); });
            test::error(invalid_resource, "download", "destination",
                        [&] { cmd.download(local, readback); });
            test::error(invalid_resource, "destroy", "resource", [&] { ctx.destroy(foreign); });
            test::error(invalid_resource, "submit", "commands",
                        [&] { (void)ctx.submit(foreign_cmd); });
            const auto flight = other.submit(foreign_cmd);
            test::error(invalid_resource, "wait", "submission", [&] { ctx.wait(flight); });
            test::error(invalid_resource, "is_complete", "submission",
                        [&] { (void)ctx.is_complete(flight); });
            other.wait(flight);
            auto moved = std::move(other);
            test::error(invalid_resource, "create_commands", "context",
                        [&] { (void)other.create_commands(); });
            moved.destroy(foreign);
            moved.destroy(upload);
            moved.destroy(readback);
            ctx.destroy(local);
        });

        test::run("invalid operation arguments never consume a command slot", [&] {
            auto image = ctx.create_image({2, 2});
            auto scratch = ctx.create_image({2, 2});
            auto small = ctx.create_image({1, 1});
            auto cmd = ctx.create_commands(1);
            const auto alias = image;
            test::error(invalid_argument, "copy", "destination", [&] { cmd.copy(image, alias); });
            test::error(invalid_argument, "resize", "destination",
                        [&] { cmd.resize(image, alias); });
            test::error(invalid_argument, "blend", "destination",
                        [&] { cmd.blend(image, alias, {.position = {0, 0}}); });
            test::error(capacity, "gaussian_blur", "workspace",
                        [&] { cmd.gaussian_blur(image, {.radius = 1, .sigma = 1}); });
            test::error(invalid_argument, "copy", "destination", [&] { cmd.copy(image, small); });
            auto too_small =
                ctx.create_workspace(gaussian_blur_requirements({1, 1}, {.radius = 1}).workspace);
            test::error(capacity, "gaussian_blur", "workspace", [&] {
                cmd.gaussian_blur(image, {.radius = 1, .sigma = 1, .workspace = too_small});
            });
            test::error(invalid_argument, "resize", "filter", [&] {
                cmd.resize(image, scratch, {.filter = static_cast<ResizeFilter>(99)});
            });
            test::error(invalid_argument, "blend", "mode", [&] {
                cmd.blend(image, scratch,
                          {.position = {0, 0}, .opacity = 1, .mode = static_cast<BlendMode>(99)});
            });
            for (const float value :
                 {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                  -std::numeric_limits<float>::infinity()}) {
                test::error(invalid_argument, "brightness", "amount",
                            [&] { cmd.brightness(image, {.amount = value}); });
                for (std::size_t component = 0; component < 4; ++component) {
                    std::array<float, 4> color{0, 0, 0, 1};
                    color[component] = value;
                    test::error(invalid_argument, "fill", "color", [&] {
                        cmd.fill(image, {.color = {color[0], color[1], color[2], color[3]}});
                    });
                }
                test::error(invalid_argument, "blend", "opacity", [&] {
                    cmd.blend(image, scratch, {.position = {0, 0}, .opacity = value});
                });
                test::error(invalid_argument, "gaussian_blur", "sigma",
                            [&] { cmd.gaussian_blur(image, {.radius = 1, .sigma = value}); });
            }
            for (const float opacity : {-0.01f, 1.01f}) {
                test::error(invalid_argument, "blend", "opacity", [&] {
                    cmd.blend(image, scratch, {.position = {0, 0}, .opacity = opacity});
                });
            }
            for (const float sigma : {0.0f, -1.0f}) {
                test::error(invalid_argument, "gaussian_blur", "sigma",
                            [&] { cmd.gaussian_blur(image, {.radius = 1, .sigma = sigma}); });
            }
            for (const auto radius : {std::int64_t{-1}, std::numeric_limits<std::int64_t>::max()}) {
                test::error(invalid_argument, "gaussian_blur", "radius",
                            [&] { cmd.gaussian_blur(image, {.radius = radius, .sigma = 1}); });
            }
            cmd.gaussian_blur(image, test::reserve_workspace(
                                         ctx, image, GaussianBlurOptions{.radius = 0, .sigma = 1},
                                         gaussian_blur_requirements));
            cmd.fill(image, {.color = {0.25f, 0.5f, 0.75f, 1}});
            ctx.set_memory_limit(ctx.memory().total);
            test::error(capacity, "brightness", "memory_limit",
                        [&] { cmd.brightness(image, {.amount = 1}); });
            ctx.set_memory_limit(0);
            ctx.submit_and_wait(cmd);
            const auto pixels = test::read(ctx, image);
            for (std::size_t i = 0; i < pixels.size(); ++i) {
                test::near(pixels[i], std::array<std::uint8_t, 4>{137, 188, 225, 255}[i % 4]);
            }
            ctx.destroy(image);
            ctx.destroy(scratch);
            ctx.destroy(small);
        });

        test::run("two-pass blur rejection is atomic", [&] {
            auto image = ctx.create_image({3, 1});
            auto scratch = ctx.create_image({3, 1});
            const std::array<float, 12> pixels{1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1};
            test::paint(ctx, image, pixels);
            auto cmd = ctx.create_commands(1);
            auto options =
                test::reserve_workspace(ctx, image, GaussianBlurOptions{.radius = 1, .sigma = 1},
                                        gaussian_blur_requirements);
            ctx.set_memory_limit(ctx.memory().total);
            test::error(capacity, "gaussian_blur", "memory_limit",
                        [&] { cmd.gaussian_blur(image, options); });
            ctx.set_memory_limit(0);
            // Neither pass may hold a resource reference or consume the remaining slot.
            ctx.destroy(options.workspace);
            ctx.destroy(scratch);
            cmd.brightness(image, {.amount = 0});
            ctx.submit_and_wait(cmd);
            const auto result = test::read(ctx, image);
            for (std::size_t i = 0; i < pixels.size(); ++i) {
                test::near(result[i], pixels[i] * 255);
            }
            ctx.destroy(image);
        });

        test::run("transfer ranges, capacity and CPU access exclusion", [&] {
            auto image = ctx.create_image({2, 1});
            auto small = ctx.create_image({1, 1});
            auto upload = ctx.create_upload_buffer(image);
            auto readback = ctx.create_readback_buffer(image);
            auto small_upload = ctx.create_upload_buffer(small);
            auto small_readback = ctx.create_readback_buffer(small);
            auto cmd = ctx.create_commands(2);
            std::array<std::uint8_t, 8> pixels{26, 51, 77, 255, 102, 128, 153, 255};
            std::array<std::uint8_t, 12> oversized{};
            test::error(invalid_argument, "write", "pixels", [&] { ctx.write(upload, {}); });
            test::error(invalid_argument, "write", "pixels",
                        [&] { ctx.write(upload, std::span(pixels).first(3)); });
            test::error(capacity, "write", "pixels", [&] { ctx.write(upload, oversized); });
            test::error(invalid_argument, "read", "pixels", [&] { ctx.read(readback, pixels); });
            test::error(invalid_argument, "read", "pixels", [&] { ctx.read(readback, {}); });
            test::error(invalid_argument, "read", "pixels",
                        [&] { ctx.read(readback, std::span(pixels).first(3)); });
            test::error(capacity, "read", "pixels", [&] { ctx.read(readback, oversized); });
            test::error(invalid_argument, "upload", "source", [&] { cmd.upload(upload, image); });
            ctx.write(upload, pixels);
            ctx.write(upload, std::span(pixels).first(4));
            test::error(invalid_argument, "upload", "source", [&] { cmd.upload(upload, image); });
            test::error(capacity, "upload", "source", [&] { cmd.upload(small_upload, image); });
            test::error(capacity, "download", "destination",
                        [&] { cmd.download(image, small_readback); });
            ctx.write(upload, pixels);
            cmd.upload(upload, image);
            cmd.download(image, readback);
            const auto check_busy = [&] {
                test::error(resource_busy, "write", "buffer", [&] { ctx.write(upload, pixels); });
                test::error(resource_busy, "read", "buffer", [&] { ctx.read(readback, pixels); });
                test::error(resource_busy, "destroy", "resource", [&] { ctx.destroy(image); });
                test::error(resource_busy, "destroy", "resource", [&] { ctx.destroy(upload); });
                test::error(resource_busy, "destroy", "resource", [&] { ctx.destroy(readback); });
            };
            check_busy();
            const auto flight = ctx.submit(cmd);
            // Pending remains a public state until wait, regardless of GPU completion.
            check_busy();
            ctx.wait(flight);
            test::error(invalid_argument, "read", "pixels",
                        [&] { ctx.read(readback, std::span(pixels).first(4)); });
            std::array<std::uint8_t, 8> result{};
            ctx.read(readback, result);
            for (std::size_t i = 0; i < pixels.size(); ++i) {
                test::near(result[i], pixels[i]);
            }
            ctx.write(upload, pixels);
            ctx.destroy(image);
            ctx.destroy(small);
            ctx.destroy(upload);
            ctx.destroy(readback);
            ctx.destroy(small_upload);
            ctx.destroy(small_readback);
        });
    });
    return test::finish();
}
