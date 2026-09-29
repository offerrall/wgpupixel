#include "test.h"

using namespace wgpupixel;

int main() {
    test::run("revisions follow submitted writes and aliases, not pixel equality", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({4, 4});
        auto alias = image;
        auto other = ctx.create_image({4, 4});
        auto scratch = ctx.create_image({4, 4});
        auto mask = ctx.create_mask({4, 4});
        auto cmd = ctx.create_commands(8);
        test::check(image.revision() == 0, "new resource revision");
        {
            auto discarded = ctx.create_commands(1);
            discarded.fill(image, {.color = {1, 0, 0, 1}});
        }
        test::check(image.revision() == 0, "discarded recording changed revision");
        cmd.fill(alias, {.color = {1, 0, 0, 1}});
        test::check(image.revision() == 0, "recording changed revision");
        auto flight = ctx.submit(cmd);
        test::check(image.revision() == 1 && alias.revision() == 1,
                    "submission must update all aliases before wait");
        ctx.wait(flight);
        cmd.fill(image, {.color = {1, 0, 0, 1}});
        ctx.submit_and_wait(cmd);
        test::check(image.revision() == 2, "identical output is still a write");
        const auto pixels = test::read(ctx, image);
        test::check(image.revision() == 2, "download changed source revision");
        cmd.copy(image, other);
        ctx.submit_and_wait(cmd);
        test::check(image.revision() == 2 && other.revision() == 1, "copy access tracking");
        cmd.fill(mask, {.coverage = 0.5f});
        cmd.grayscale(other, {.mask = &mask});
        cmd.gaussian_blur(other, test::reserve_workspace(
                                     ctx, other, GaussianBlurOptions{.radius = 1, .sigma = 1.0f},
                                     gaussian_blur_requirements));
        ctx.submit_and_wait(cmd);
        test::check(other.revision() == 3 && scratch.revision() == 0 && mask.revision() == 1,
                    "in-place and coverage access tracking");
        auto upload = ctx.create_upload_buffer(image);
        ctx.write(upload, pixels);
        cmd.upload(upload, image);
        ctx.submit_and_wait(cmd);
        test::check(image.revision() == 3, "upload did not count as a write");
        const Rect empty{0, 0, 0, 4};
        cmd.fill(image, {.color = {0, 0, 0, 0}, .region = empty});
        cmd.gaussian_blur(image, test::reserve_workspace(
                                     ctx, image, GaussianBlurOptions{.radius = 0, .sigma = 1.0f},
                                     gaussian_blur_requirements));
        ctx.submit_and_wait(cmd);
        test::check(image.revision() == 3 && scratch.revision() == 0,
                    "elided operations changed revision");
        image.set_size({4, 4});
        test::check(image.revision() == 3, "unchanged dimensions changed revision");
        image.set_size({2, 8});
        image.set_size({4, 4});
        test::check(alias.revision() == 5, "shape changes must count even when restored");
        mask.set_size({2, 8});
        test::check(mask.revision() == 2, "mask dimensions must count");
        test::error(ErrorCode::capacity, "set_size", "size", [&] { image.set_size({5, 4}); });
        test::error(ErrorCode::invalid_argument, "copy", "destination",
                    [&] { cmd.copy(image, alias); });
        test::check(image.revision() == 5, "rejected operation changed revision");
        ctx.destroy(image);
        test::error(ErrorCode::invalid_resource, "revision", "resource",
                    [&] { (void)alias.revision(); });
    });
    test::run("rejected submission leaves revisions unchanged", [] {
        auto ctx = Context::create();
        auto other = Context::create();
        auto image = ctx.create_image({1, 1});
        auto cmd = ctx.create_commands(1);
        cmd.fill(image, {.color = {0, 0, 0, 0}});
        test::error(ErrorCode::invalid_resource, "submit", "commands",
                    [&] { (void)other.submit(cmd); });
        test::check(image.revision() == 0, "rejected submission changed revision");
        ctx.submit_and_wait(cmd);
        test::check(image.revision() == 1, "retry must count once");
    });
    return test::finish();
}
