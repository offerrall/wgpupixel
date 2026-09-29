#include "../test.h"
#include <array>
#include <limits>
using namespace wgpupixel;
int main() {
    test::run("temporary sizes without a GPU context", [] {
        const auto blur = gaussian_blur_requirements({320, 200}, {});
        test::check(blur.destination == ImageSize{320, 200} && blur.workspace.bytes() == 0,
                    "zero support needs no internal scratch");

        const auto blurred = gaussian_blur_requirements({320, 200}, {.radius = 1});
        test::check(blurred.workspace.bytes() == 320 * 200 * 16,
                    "small blur uses one full-size plane");

        const auto clipped = drop_shadow_requirements(
            {320, 200}, {.offset = {-12, 10}, .radius = 10, .expand = false});
        test::check(clipped.destination.width == 320 && clipped.destination.height == 200,
                    "clipped output dimensions");
        test::check(clipped.workspace.bytes() == 2 * 340 * 220 * 16,
                    "clipping must retain full temporary padding");
        const auto expanded =
            drop_shadow_requirements({320, 200}, {.offset = {-12, 10}, .radius = 10});
        test::check(expanded.destination.width == 352 && expanded.destination.height == 230,
                    "expanded signed offset dimensions");
        const auto zero = drop_shadow_requirements({1, 1}, {.offset = {0, 0}, .radius = 0});
        test::check(zero.workspace.bytes() == 16 &&
                        expanded.workspace.bytes() == 2 * 340 * 220 * 16,
                    "shadow uses caller-provided workspace");

        test::error(ErrorCode::invalid_argument, "drop_shadow_requirements", "sigma",
                    [] { (void)drop_shadow_requirements({1, 1}, {.sigma = 0}); });
        test::error(ErrorCode::invalid_argument, "drop_shadow_requirements", "color",
                    [] { (void)drop_shadow_requirements({1, 1}, {.color = {0, 0, 0, 2}}); });
        test::check(zero.destination == ImageSize{1, 1}, "zero radius dimensions");
        test::error(ErrorCode::invalid_argument, "gaussian_blur_requirements", "source",
                    [] { (void)gaussian_blur_requirements({0, 1}, {}); });
        test::error(ErrorCode::invalid_argument, "drop_shadow_requirements", "source", [] {
            (void)drop_shadow_requirements({-1, 1}, {.offset = {0, 0}, .radius = 0});
        });
        test::error(ErrorCode::invalid_argument, "drop_shadow_requirements", "radius", [] {
            (void)drop_shadow_requirements({1, 1}, {.offset = {0, 0}, .radius = -1});
        });
        test::error(ErrorCode::invalid_argument, "drop_shadow_requirements", "radius", [] {
            (void)drop_shadow_requirements(
                {1, 1}, {.offset = {0, 0}, .radius = std::numeric_limits<std::int64_t>::max()});
        });
        test::error(ErrorCode::invalid_argument, "drop_shadow_requirements", "radius", [] {
            (void)drop_shadow_requirements({2147483647, 1}, {.offset = {0, 0}, .radius = 1});
        });
        test::error(ErrorCode::invalid_argument, "drop_shadow_requirements", "offset", [] {
            (void)drop_shadow_requirements(
                {1, 1}, {.offset = {std::numeric_limits<std::int32_t>::min(), 0}, .radius = 0});
        });
        test::error(ErrorCode::invalid_argument, "drop_shadow_requirements", "offset", [] {
            (void)drop_shadow_requirements({1, 1},
                                           {.offset = {std::numeric_limits<std::int32_t>::min(), 0},
                                            .radius = 1,
                                            .expand = false});
        });
    });
    test::run("drop shadow padded gaussian mask and source over preserve alpha", [] {
        auto ctx = Context::create();
        auto src = ctx.create_image({1, 1});
        const auto sizes = drop_shadow_requirements(src.size(), {.offset = {-1, 1}, .radius = 1});
        auto dst = ctx.create_image(sizes.destination);
        auto workspace = ctx.create_workspace(sizes.workspace);
        auto cmd = ctx.create_commands();
        cmd.fill(src, {.color = {.2f, .1f, 0, .5f}});
        cmd.drop_shadow(src, dst,
                        {.offset = {-1, 1},
                         .radius = 1,
                         .sigma = 1,
                         .color = {.1f, .2f, .3f, .5f},
                         .workspace = workspace});
        ctx.submit_and_wait(cmd);
        std::array<float, 64> expected{};
        const float w = std::exp(-.5f), norm = 1 + 2 * w;
        for (int y = 1; y < 4; ++y) {
            for (int x = 0; x < 3; ++x) {
                const float weight = (x == 1 ? 1 : w) * (y == 2 ? 1 : w) / (norm * norm) * .5f;
                const auto i = (y * 4 + x) * 4;
                expected[i] = .1f * weight;
                expected[i + 1] = .2f * weight;
                expected[i + 2] = .3f * weight;
                expected[i + 3] = .5f * weight;
            }
        }
        const auto i = (1 * 4 + 2) * 4;
        expected[i] = .2f + .5f * expected[i];
        expected[i + 1] = .1f + .5f * expected[i + 1];
        expected[i + 2] *= .5f;
        expected[i + 3] = .5f + .5f * expected[i + 3];
        const auto a = test::read(ctx, dst), b = test::encode(expected);
        for (std::size_t j = 0; j < a.size(); ++j) {
            test::check(std::abs(int(a[j]) - int(b[j])) <= 1, "padded shadow CPU mismatch");
        }
        const auto source = test::read(ctx, src);
        test::check(source[3] == 128, "source modified");
    });
    test::run("zero radius shadow and atomic capacity failure", [] {
        auto ctx = Context::create();
        auto src = ctx.create_image({1, 1});
        auto dst = ctx.create_image({1, 1});
        auto workspace =
            ctx.create_workspace(drop_shadow_requirements(src.size(), {.radius = 0}).workspace);
        auto init = ctx.create_commands(2);
        init.fill(src, {.color = {.2f, .1f, 0, .5f}});
        init.fill(dst, {.color = {0, 0, 0, 1}});
        ctx.submit_and_wait(init);
        auto small = ctx.create_commands(3);
        ctx.set_memory_limit(ctx.memory().total);
        test::error(ErrorCode::capacity, "drop_shadow", "memory_limit", [&] {
            small.drop_shadow(src, dst,
                              {.offset = {0, 0},
                               .radius = 0,
                               .sigma = 1,
                               .color = {.1f, .2f, .3f, .5f},
                               .workspace = workspace});
        });
        ctx.set_memory_limit(0);
        small.fill(dst, {.color = {0, 0, 0, 1}});
        ctx.submit_and_wait(small);
        auto cmd = ctx.create_commands(4);
        cmd.drop_shadow(src, dst,
                        {.offset = {0, 0},
                         .radius = 0,
                         .sigma = 1,
                         .color = {.1f, .2f, .3f, .5f},
                         .expand = false,
                         .workspace = workspace});
        ctx.submit_and_wait(cmd);
        const std::array<float, 4> expected{.225f, .15f, .075f, .625f};
        const auto a = test::read(ctx, dst), b = test::encode(expected);
        for (std::size_t j = 0; j < 4; ++j) {
            test::check(std::abs(int(a[j]) - int(b[j])) <= 1, "zero radius shadow mismatch");
        }
        test::error(ErrorCode::capacity, "drop_shadow", "workspace", [&] {
            cmd.drop_shadow(src, dst,
                            {.offset = {0, 0}, .radius = 0, .sigma = 1, .color = {0, 0, 0, 1}});
        });
        test::error(ErrorCode::invalid_argument, "drop_shadow", "sigma", [&] {
            cmd.drop_shadow(src, dst,
                            {.offset = {0, 0}, .radius = 0, .sigma = 0, .color = {0, 0, 0, 1}});
        });
        test::error(ErrorCode::invalid_argument, "drop_shadow", "destination", [&] {
            cmd.drop_shadow(src, dst,
                            {.offset = {1, 0}, .radius = 0, .sigma = 1, .color = {0, 0, 0, 1}});
        });
        cmd.fill(dst, {.color = {0, 0, 0, 1}});
        ctx.submit_and_wait(cmd);
    });
    test::run("clipped shadow blurs before clipping and retains all resources", [] {
        auto ctx = Context::create();
        auto src = ctx.create_image({1, 1});
        auto dst = ctx.create_image({1, 1});
        auto workspace =
            ctx.create_workspace(drop_shadow_requirements(src.size(), {.radius = 1}).workspace);
        auto init = ctx.create_commands(1);
        init.fill(src, {.color = {.2f, .1f, 0, .5f}});
        ctx.submit_and_wait(init);
        auto small = ctx.create_commands(5);
        ctx.set_memory_limit(ctx.memory().total);
        test::error(ErrorCode::capacity, "drop_shadow", "memory_limit", [&] {
            small.drop_shadow(src, dst,
                              {.offset = {1, 0},
                               .radius = 1,
                               .sigma = 1,
                               .color = {.1f, .2f, .3f, .5f},
                               .expand = false,
                               .workspace = workspace});
        });
        ctx.set_memory_limit(0);
        for (int i = 0; i < 5; ++i) {
            small.fill(dst, {.color = {0, 0, 0, 0}});
        }
        ctx.submit_and_wait(small);
        const float w = std::exp(-.5f), norm = 1 + 2 * w;
        const float weight = .5f * w / (norm * norm);
        const std::array<float, 4> expected{.2f + .05f * weight, .1f + .1f * weight, .15f * weight,
                                            .5f + .25f * weight};
        const auto reference = test::encode(expected);
        auto cmd = ctx.create_commands(6);
        for (const auto offset :
             {Position{-1, 0}, Position{1, 0}, Position{0, -1}, Position{0, 1}}) {
            cmd.drop_shadow(src, dst,
                            {.offset = offset,
                             .radius = 1,
                             .sigma = 1,
                             .color = {.1f, .2f, .3f, .5f},
                             .expand = false,
                             .workspace = workspace});
            for (const auto& resource : {src, dst}) {
                test::error(ErrorCode::resource_busy, "destroy", "resource",
                            [&] { ctx.destroy(resource); });
            }
            test::error(ErrorCode::resource_busy, "destroy", "workspace",
                        [&] { ctx.destroy(workspace); });
            ctx.submit_and_wait(cmd);
            const auto actual = test::read(ctx, dst);
            for (std::size_t i = 0; i < actual.size(); ++i) {
                test::check(std::abs(int(actual[i]) - int(reference[i])) <= 1,
                            "shadow clipped before convolution or wrong signed offset");
            }
        }
        for (const auto& resource : {src, dst}) {
            ctx.destroy(resource);
        }
    });
    return test::finish();
}
