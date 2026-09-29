#include "../test.h"
#include "../../src/utils/workspace.h"

#include <array>
#include <functional>

using namespace wgpupixel;
using enum ErrorCode;

namespace {
// The largest slot of a plan shrunk by one float pixel, so exactly one slot is short.
WorkspacePlan short_by_one_pixel(const WorkspacePlan& plan) {
    auto slots = detail::WorkspaceAccess::slots(plan);
    slots.front() -= 16;
    WorkspacePlan result;
    for (const auto bytes : slots) detail::workspace_buffer(result, bytes);
    return result;
}

// A geometry call that needs a workspace succeeds with exactly its plan and is
// rejected, without recording, when one slot is a pixel short.
template <class Options, class Record>
void plan_is_exact(Context& ctx, const WorkspacePlan& plan, Options options, Record record,
                   std::string_view operation) {
    test::check(plan.bytes() > 0, "the case must need a workspace");
    auto cmd = ctx.create_commands();
    options.workspace = ctx.create_workspace(short_by_one_pixel(plan));
    test::error(capacity, operation, "workspace", [&] { record(cmd, options); });
    options.workspace = ctx.create_workspace(plan);
    record(cmd, options);
    ctx.submit_and_wait(cmd);
}
} // namespace

int main() {
    test::run("small geometry needs no workspace", [] {
        test::check(resize_requirements({64, 64}, {32, 32}, {}).workspace.bytes() == 0,
                    "a halving resize samples directly");
        test::check(resize_requirements({4096, 4096}, {3, 3}, {.filter = ResizeFilter::nearest})
                            .workspace.bytes() == 0,
                    "nearest never splits");
        test::check(transform_requirements({64, 64}, {64, 64},
                                           TransformOptions{.matrix = Affine::rotate(30)})
                            .workspace.bytes() == 0,
                    "a rotation samples directly");
        const auto sized = resize_requirements({64, 32}, {7, 9}, {});
        test::check(sized.destination == ImageSize{7, 9}, "the destination size is reported");
    });

    test::run("geometry plans are exact for images and masks", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({1024, 512}), small = ctx.create_image({9, 9});
        auto mask = ctx.create_mask({1024, 512}), small_mask = ctx.create_mask({9, 9});
        const ResizeOptions area{.filter = ResizeFilter::area};
        const auto resize_plan = resize_requirements(image.size(), small.size(), area).workspace;
        plan_is_exact(ctx, resize_plan, area,
                      [&](Commands& c, const ResizeOptions& o) { c.resize(image, small, o); },
                      "resize");
        plan_is_exact(ctx, resize_plan, area,
                      [&](Commands& c, const ResizeOptions& o) { c.resize(mask, small_mask, o); },
                      "resize");

        auto wide = ctx.create_image({650, 40}), narrow = ctx.create_image({10, 40});
        for (const auto edge : {EdgeMode::transparent, EdgeMode::repeat}) {
            const TransformOptions reduce{.matrix = Affine::scale(1.0f / 65, 1),
                                          .filter = ResizeFilter::area, .edge = edge};
            plan_is_exact(ctx, transform_requirements(wide.size(), narrow.size(), reduce).workspace,
                          reduce,
                          [&](Commands& c, const TransformOptions& o) { c.transform(wide, narrow, o); },
                          "transform");
        }
        const TransformOptions shrink{.matrix = Affine::rotate(20) * Affine::scale(0.05f),
                                      .filter = ResizeFilter::bicubic};
        plan_is_exact(ctx, transform_requirements(image.size(), small.size(), shrink).workspace,
                      shrink,
                      [&](Commands& c, const TransformOptions& o) { c.transform(image, small, o); },
                      "transform");

        auto canvas = ctx.create_image({64, 64});
        const PerspectiveOptions tilt{
            .corners = {Point{20, 0}, Point{44, 0}, Point{64, 64}, Point{0, 64}},
            .filter = ResizeFilter::bilinear};
        const auto tilt_plan = perspective_requirements(image.size(), canvas.size(), tilt).workspace;
        plan_is_exact(ctx, tilt_plan, tilt,
                      [&](Commands& c, const PerspectiveOptions& o) { c.perspective(image, canvas, o); },
                      "perspective");
        auto canvas_mask = ctx.create_mask({64, 64});
        plan_is_exact(ctx, tilt_plan, tilt,
                      [&](Commands& c, const PerspectiveOptions& o) {
                          c.perspective(mask, canvas_mask, o);
                      },
                      "perspective");
    });

    test::run("geometry queries validate like the operations", [] {
        test::error(invalid_argument, "resize_requirements", "source",
                    [] { (void)resize_requirements({0, 4}, {4, 4}, {}); });
        test::error(invalid_argument, "resize_requirements", "filter", [] {
            (void)resize_requirements({4, 4}, {2, 2}, {.filter = static_cast<ResizeFilter>(9)});
        });
        test::error(invalid_argument, "transform_requirements", "matrix", [] {
            (void)transform_requirements({4, 4}, {4, 4}, TransformOptions{.matrix = Affine::scale(0)});
        });
        test::error(invalid_argument, "transform_requirements", "region", [] {
            (void)transform_requirements({4, 4}, {4, 4},
                                         TransformOptions{.region = Rect{0, 0, -1, 2}});
        });
        test::error(invalid_argument, "perspective_requirements", "corners", [] {
            (void)perspective_requirements({4, 4}, {4, 4}, {});
        });
    });

    return test::finish();
}
