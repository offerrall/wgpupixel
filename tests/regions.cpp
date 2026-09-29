#include "test.h"
#include <limits>

using namespace wgpupixel;

namespace {
void expect_region(Context& context, const Image& partial, const Image& full,
                   const std::vector<std::uint8_t>& before, Rect region) {
    const auto actual = test::read(context, partial);
    const auto expected = test::read(context, full);
    for (std::int32_t y = 0; y < std::int32_t(partial.size().height); ++y) {
        for (std::int32_t x = 0; x < std::int32_t(partial.size().width); ++x) {
            const bool inside = x >= region.x && y >= region.y &&
                                std::int64_t(x) < std::int64_t(region.x) + region.width &&
                                std::int64_t(y) < std::int64_t(region.y) + region.height;
            for (unsigned channel = 0; channel < 4; ++channel) {
                const auto index = (y * partial.size().width + x) * 4 + channel;
                test::near(actual[index], inside ? expected[index] : before[index], 0);
            }
        }
    }
}
} // namespace

int main() {
    test::run("region clips signed bounds, preserves pixels, validates extents", [] {
        auto context = Context::create();
        auto image = context.create_image({13, 11});
        auto full = context.create_image({13, 11});
        auto commands = context.create_commands(8);
        commands.fill(image, {.color = {0, 0, 1, 1}});
        commands.fill(full, {.color = {1, 0, 0, 1}});
        context.submit_and_wait(commands);
        const auto before = test::read(context, image);
        const Rect region{-2, 3, 7, 20};
        const auto options = [region] {
            auto local = region;
            const FillOptions result{.color = {1, 0, 0, 1}, .region = local};
            local = {0, 0, 0, 0}; // Options own a snapshot, independent of this local's lifetime.
            return result;
        }();
        commands.fill(image, options);
        context.submit_and_wait(commands);
        expect_region(context, image, full, before, region);
        const auto unchanged = test::read(context, image);
        const Rect empty{0, 0, 0, 4};
        const Rect remote{std::numeric_limits<std::int32_t>::max(), 0, 10, 10};
        commands.invert(image, {.region = empty});
        commands.invert(image, {.region = remote});
        context.submit_and_wait(commands);
        test::check(test::read(context, image) == unchanged, "empty region changed pixels");
        const Rect invalid{0, 0, -1, 1};
        test::error(ErrorCode::invalid_argument, "fill", "region",
                    [&] { commands.fill(image, {.color = {0, 0, 0, 0}, .region = invalid}); });
    });

    test::run("region and mask keep destination coordinates for positioned blend", [] {
        auto context = Context::create();
        auto source = context.create_image({7, 9});
        auto partial = context.create_image({13, 11});
        auto full = context.create_image({13, 11});
        auto mask = context.create_mask({13, 11});
        auto upload = context.create_upload_buffer(mask);
        std::vector<std::uint8_t> coverage(13 * 11);
        for (std::size_t i = 0; i < coverage.size(); ++i) {
            coverage[i] = (i * 31) % 256;
        }
        context.write(upload, coverage);
        auto commands = context.create_commands(8);
        commands.upload(upload, mask);
        commands.fill(source, {.color = {.8f, .2f, .1f, 1}});
        commands.fill(partial, {.color = {.1f, .3f, .5f, 1}});
        commands.copy(partial, full);
        context.submit_and_wait(commands);
        const auto before = test::read(context, partial);
        const Rect region{2, 3, 7, 4};
        commands.blend(source, partial,
                       {.position = {-1, 2},
                        .opacity = .7f,
                        .mode = BlendMode::screen,
                        .mask = &mask,
                        .region = region});
        commands.blend(
            source, full,
            {.position = {-1, 2}, .opacity = .7f, .mode = BlendMode::screen, .mask = &mask});
        context.submit_and_wait(commands);
        expect_region(context, partial, full, before, region);
    });

    test::run("region copy and resize retain global sampling coordinates", [] {
        auto context = Context::create();
        auto source = context.create_image({13, 11});
        auto partial = context.create_image({13, 11});
        auto full = context.create_image({13, 11});
        auto commands = context.create_commands(8);
        const std::array stops{GradientStop{0, {1, 0, 0, 1}}, GradientStop{1, {0, 0, 1, 1}}};
        commands.gradient_fill(source, {.start = {0.5f, 0},
                                        .end = {float(source.size().width) - 0.5f, 0},
                                        .stops = stops,
                                        .interpolation = GradientInterpolation::linear,
                                        .dither = false,
                                        .replace = true});
        commands.fill(partial, {.color = {0, 1, 0, 1}});
        context.submit_and_wait(commands);
        const auto before = test::read(context, partial);
        const Rect region{3, 2, 5, 7};
        commands.copy(source, partial, {.region = region});
        commands.copy(source, full);
        context.submit_and_wait(commands);
        expect_region(context, partial, full, before, region);
        source.set_size({7, 5});
        commands.gradient_fill(source, {.start = {0.5f, 0},
                                        .end = {float(source.size().width) - 0.5f, 0},
                                        .stops = stops,
                                        .interpolation = GradientInterpolation::linear,
                                        .dither = false,
                                        .replace = true});
        commands.fill(partial, {.color = {0, 1, 0, 1}});
        commands.resize(source, partial, {.filter = ResizeFilter::bicubic, .region = region});
        commands.resize(source, full, {.filter = ResizeFilter::bicubic});
        context.submit_and_wait(commands);
        expect_region(context, partial, full, before, region);
    });

    test::run("regional blur reads the halo and ignores dirty scratch pixels", [] {
        auto context = Context::create();
        auto partial = context.create_image({13, 11});
        auto full = context.create_image({13, 11});
        auto scratch = context.create_image({13, 11});
        auto reference_scratch = context.create_image({13, 11});
        auto mask = context.create_mask({13, 11});
        auto commands = context.create_commands(12);
        commands.checkerboard(
            partial, {.size = 2, .first = {.8f, .2f, .1f, 1}, .second = {.1f, .3f, .5f, 1}});
        commands.copy(partial, full);
        commands.fill(scratch, {.color = {100, 100, 100, 1}});
        commands.fill(mask, {.coverage = .5f});
        context.submit_and_wait(commands);
        const auto before = test::read(context, partial);
        const Rect region{4, 3, 3, 4};
        commands.gaussian_blur(
            partial,
            test::reserve_workspace(
                context, partial,
                GaussianBlurOptions{.radius = 3, .sigma = 1.5f, .mask = &mask, .region = region},
                gaussian_blur_requirements));
        commands.gaussian_blur(
            full, test::reserve_workspace(
                      context, full, GaussianBlurOptions{.radius = 3, .sigma = 1.5f, .mask = &mask},
                      gaussian_blur_requirements));
        context.submit_and_wait(commands);
        expect_region(context, partial, full, before, region);
    });
    return test::finish();
}
