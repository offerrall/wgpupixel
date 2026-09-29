#include "../test.h"

using namespace wgpupixel;

int main() {
    test::run("stroke snapshots are explicitly reserved and counted by pixel kind", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({37, 19});
        auto mask = ctx.create_mask({37, 19});
        const auto before = ctx.memory();
        auto paint = ctx.create_brush_stroke_state(image);
        auto erase = ctx.create_brush_stroke_state(mask);
        auto smudge = ctx.create_smudge_stroke_state(image);
        test::check(ctx.memory().images == before.images + 2 * 37 * 19 * 16,
                    "image stroke snapshots must be counted as images");
        test::check(ctx.memory().masks == before.masks + 704,
                    "mask stroke snapshot must be four-byte aligned and counted as masks");
        const auto reserved = ctx.memory().total;
        paint.reset();
        erase.reset();
        smudge.reset();
        test::check(ctx.memory().total == reserved, "reset must retain snapshot capacity");
        paint = {};
        erase = {};
        smudge = {};
        test::check(ctx.memory().total == before.total,
                    "discarding idle states releases snapshots");
        ctx.set_memory_limit(before.total + 37 * 19 * 16 - 1);
        test::error(ErrorCode::capacity, "create_brush_stroke_state", "memory_limit",
                    [&] { (void)ctx.create_brush_stroke_state(image); });
        test::check(ctx.memory().total == before.total,
                    "failed reservation must not retain storage");
    });
    test::run("continuation validates a reserved state before recording", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({37, 19});
        auto small = ctx.create_image({1, 1});
        auto cmd = ctx.create_commands();
        cmd.fill(image, {.color = {0, 1, 0, 1}});
        BrushStrokeState empty;
        test::error(ErrorCode::invalid_resource, "brush_stroke", "state",
                    [&] { cmd.brush_stroke(image, empty, {}); });
        auto wrong_size = ctx.create_brush_stroke_state(small);
        test::error(ErrorCode::invalid_argument, "brush_stroke", "destination",
                    [&] { cmd.brush_stroke(image, wrong_size, {}); });
        auto other = Context::create();
        auto other_image = other.create_image(image.size());
        auto foreign = other.create_brush_stroke_state(other_image);
        test::error(ErrorCode::invalid_resource, "brush_stroke", "state",
                    [&] { cmd.brush_stroke(image, foreign, {}); });
        ctx.submit_and_wait(cmd);
        const auto pixels = test::read(ctx, image);
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            test::check(pixels[i] == std::array<std::uint8_t, 4>{0, 255, 0, 255}[i % 4],
                        "state validation changed earlier commands");
        }
    });
    test::run("reset captures a new destination without growing GPU storage", [] {
        auto ctx = Context::create();
        auto first = ctx.create_image({37, 19}), next = ctx.create_image({37, 19});
        auto reference = ctx.create_image({37, 19});
        auto state = ctx.create_brush_stroke_state(first);
        const std::array samples{StrokeSample{{4, 5}}, StrokeSample{{30, 14}}};
        const BrushStrokeOptions options{.samples = samples,
                                         .brush = {.diameter = 7, .hardness = .5f},
                                         .color = {-.2f, .6f, .1f, .7f},
                                         .opacity = .6f};
        auto cmd = ctx.create_commands(256);
        cmd.fill(first, {.color = {1, 0, 0, 1}});
        const auto reserved = ctx.memory().images;
        cmd.brush_stroke(first, state, options);
        test::check(ctx.memory().images == reserved, "continuation allocated another snapshot");
        ctx.submit_and_wait(cmd);
        test::error(ErrorCode::invalid_argument, "brush_stroke", "state",
                    [&] { cmd.brush_stroke(next, state, options); });
        state.reset();
        cmd.fill(next, {.color = {0, .25f, .5f, 1}});
        cmd.fill(reference, {.color = {0, .25f, .5f, 1}});
        cmd.brush_stroke(next, state, options);
        cmd.brush_stroke(reference, options);
        test::check(ctx.memory().images == reserved, "reset changed the reserved snapshot size");
        ctx.submit_and_wait(cmd);
        test::check(test::read(ctx, next) == test::read(ctx, reference),
                    "reset reused the previous stroke snapshot or samples");
    });
    test::run("pending continuation retains discarded state snapshot", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({37, 19});
        const auto before = ctx.memory().images;
        auto state = ctx.create_brush_stroke_state(image);
        auto cmd = ctx.create_commands();
        const std::array samples{StrokeSample{{18, 9}}};
        cmd.fill(image, {.color = {0, 0, 0, 1}});
        cmd.brush_stroke(image, state,
                         {.samples = samples, .brush = {.diameter = 5}, .color = {1, 0, 0, 1}});
        const auto done = ctx.submit(cmd);
        state = {};
        cmd = {};
        test::check(ctx.memory().images == before + 37 * 19 * 16,
                    "pending continuation released its snapshot");
        ctx.wait(done);
        test::check(ctx.memory().images == before, "retirement retained discarded snapshot");
    });
    test::run("discarding a stroke recording requires reset before continuing", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({37, 19}), reference = ctx.create_image({37, 19});
        auto state = ctx.create_brush_stroke_state(image);
        const std::array first{StrokeSample{{5, 9}}};
        const std::array next{StrokeSample{{30, 9}}};
        const Brush brush{.diameter = 7, .hardness = .5f};
        const Color color{1, 0, 0, 1};
        for (bool submitted_first : {false, true}) {
            state.reset();
            ctx.run_and_wait([&](Commands& cmd) { cmd.fill(image, {.color = {0, 1, 0, 1}}); });
            {
                auto discarded = ctx.create_commands();
                discarded.brush_stroke(image, state,
                                        {.samples = first, .brush = brush, .color = color});
                if (submitted_first) {
                    ctx.submit_and_wait(discarded);
                    discarded.brush_stroke(image, state,
                                            {.samples = next, .brush = brush, .color = color});
                }
            }
            const auto before = test::read(ctx, image);
            auto cmd = ctx.create_commands();
            test::error(ErrorCode::invalid_resource, "brush_stroke", "state", [&] {
                cmd.brush_stroke(image, state,
                                 {.samples = next, .brush = brush, .color = color});
            });
            test::check(test::read(ctx, image) == before,
                        "rejected continuation changed its destination");
            state.reset();
            cmd.copy(image, reference);
            cmd.brush_stroke(image, state,
                             {.samples = next, .brush = brush, .color = color});
            cmd.brush_stroke(reference,
                             {.samples = next, .brush = brush, .color = color});
            ctx.submit_and_wait(cmd);
            test::check(test::read(ctx, image) == test::read(ctx, reference),
                        "reset after discard did not capture the current destination");
        }
    });
    test::run("multiple recorded segments and a rejected edit retain valid continuation", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({37, 19}), reference = ctx.create_image({37, 19});
        auto state = ctx.create_brush_stroke_state(image);
        const std::array samples{StrokeSample{{4, 5}}, StrokeSample{{18, 9}},
                                 StrokeSample{{30, 14}}};
        const Brush brush{.diameter = 7, .hardness = .5f, .spacing = .2f};
        const Color color{1, 0, 0, 1};
        auto cmd = ctx.create_commands();
        cmd.fill(image, {.color = {0, 1, 0, 1}});
        cmd.fill(reference, {.color = {0, 1, 0, 1}});
        cmd.brush_stroke(image, state,
                         {.samples = std::span(samples).first(1), .brush = brush, .color = color,
                          .opacity = .6f});
        test::error(ErrorCode::invalid_argument, "brush_stroke", "flow", [&] {
            cmd.brush_stroke(image, state,
                             {.samples = std::span(samples).subspan(1, 1), .brush = brush,
                              .color = color, .opacity = .6f, .flow = 2});
        });
        cmd.brush_stroke(image, state,
                         {.samples = std::span(samples).subspan(1, 1), .brush = brush,
                          .color = color, .opacity = .6f});
        ctx.submit_and_wait(cmd);
        cmd.brush_stroke(image, state,
                         {.samples = std::span(samples).last(1), .brush = brush, .color = color,
                          .opacity = .6f});
        cmd.brush_stroke(reference,
                         {.samples = samples, .brush = brush, .color = color, .opacity = .6f});
        ctx.submit_and_wait(cmd);
        test::check(test::read(ctx, image) == test::read(ctx, reference),
                    "valid continuation lost samples or changed the stroke opacity");
    });
    test::run("discarding recordings from a reset stroke does not invalidate its replacement", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({37, 19}), reference = ctx.create_image({37, 19});
        auto state = ctx.create_brush_stroke_state(image);
        const std::array samples{StrokeSample{{5, 9}}, StrokeSample{{30, 9}}};
        const Brush brush{.diameter = 7};
        const Color color{1, 0, 0, 1};
        auto old = ctx.create_commands();
        old.brush_stroke(image, state,
                         {.samples = std::span(samples).first(1), .brush = brush, .color = color});
        state.reset();
        auto cmd = ctx.create_commands();
        cmd.fill(image, {.color = {0, 1, 0, 1}});
        cmd.fill(reference, {.color = {0, 1, 0, 1}});
        cmd.brush_stroke(image, state,
                         {.samples = std::span(samples).first(1), .brush = brush, .color = color});
        old = {};
        cmd.brush_stroke(image, state,
                         {.samples = std::span(samples).last(1), .brush = brush, .color = color});
        cmd.brush_stroke(reference, {.samples = samples, .brush = brush, .color = color});
        ctx.submit_and_wait(cmd);
        test::check(test::read(ctx, image) == test::read(ctx, reference),
                    "discarded previous stroke invalidated a new state generation");
    });
    test::run("discard invalidates mask and smudge continuation states", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({37, 19});
        auto mask = ctx.create_mask(image.size());
        auto mask_state = ctx.create_brush_stroke_state(mask);
        auto smudge_state = ctx.create_smudge_stroke_state(image);
        const Brush brush{.diameter = 7};
        auto workspace = ctx.create_workspace(
            smudge_stroke_requirements(image.size(), {.brush = brush}).workspace);
        const std::array samples{StrokeSample{{5, 9}}, StrokeSample{{30, 9}}};
        {
            auto discarded = ctx.create_commands();
            discarded.brush_stroke(mask, mask_state, {.samples = samples, .brush = brush});
            discarded.smudge_stroke(image, smudge_state,
                                    {.samples = samples, .brush = brush, .workspace = workspace});
        }
        auto cmd = ctx.create_commands();
        test::error(ErrorCode::invalid_resource, "brush_stroke", "state", [&] {
            cmd.brush_stroke(mask, mask_state, {.samples = samples, .brush = brush});
        });
        test::error(ErrorCode::invalid_resource, "smudge_stroke", "state", [&] {
            cmd.smudge_stroke(image, smudge_state,
                              {.samples = samples, .brush = brush, .workspace = workspace});
        });
    });
    test::run("submitting a recorded continuation rejects its discarded predecessor", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({37, 19});
        auto state = ctx.create_brush_stroke_state(image);
        ctx.run_and_wait([&](Commands& cmd) { cmd.fill(image, {.color = {0, 1, 0, 1}}); });
        const auto before = test::read(ctx, image);
        const std::array samples{StrokeSample{{5, 9}}, StrokeSample{{30, 9}}};
        const Brush brush{.diameter = 7};
        auto first = ctx.create_commands(), next = ctx.create_commands();
        first.brush_stroke(image, state,
                           {.samples = std::span(samples).first(1), .brush = brush,
                            .color = {1, 0, 0, 1}});
        next.brush_stroke(image, state,
                          {.samples = std::span(samples).last(1), .brush = brush,
                           .color = {1, 0, 0, 1}});
        first = {};
        test::error(ErrorCode::invalid_resource, "submit", "state",
                    [&] { (void)ctx.submit(next); });
        test::check(test::read(ctx, image) == before,
                    "invalid dependent recording restored an uninitialized snapshot");
    });
    test::run("focus and smudge reuse reserved planes without growing GPU scratch", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({37, 19});
        const auto focus = focus_stroke_requirements(image.size());
        const auto smudge = smudge_stroke_requirements(image.size(), {.brush = {.diameter = 20}});
        test::check(focus.destination == image.size() && focus.workspace.bytes() == 37 * 19 * 16,
                    "focus requires one destination-sized float RGBA plane");
        test::check(smudge.destination == image.size() && smudge.workspace.bytes() == 25 * 25 * 16,
                    "diameter 20 smudge needs a 25 by 25 carried patch");
        auto plan = focus.workspace;
        plan.merge(smudge.workspace);
        auto workspace = ctx.create_workspace(plan);
        const auto before = ctx.memory();
        auto cmd = ctx.create_commands();
        const std::array samples{StrokeSample{{5, 9}}, StrokeSample{{30, 9}}};
        cmd.fill(image, {.color = {-.25f, 2, 0, 1}});
        test::error(ErrorCode::capacity, "focus_stroke", "workspace",
                    [&] { cmd.focus_stroke(image, {.samples = samples}); });
        test::error(ErrorCode::capacity, "smudge_stroke", "workspace",
                    [&] { cmd.smudge_stroke(image, {.samples = samples}); });
        auto foreign = Context::create();
        auto foreign_workspace = foreign.create_workspace(plan);
        test::error(ErrorCode::invalid_resource, "focus_stroke", "workspace", [&] {
            cmd.focus_stroke(image, {.samples = samples, .workspace = foreign_workspace});
        });
        for (int i = 0; i < 3; ++i) {
            cmd.focus_stroke(image, {.samples = samples, .workspace = workspace});
            cmd.smudge_stroke(
                image, {.samples = samples, .brush = {.diameter = 20}, .workspace = workspace});
        }
        test::check(ctx.memory().workspace == before.workspace &&
                        ctx.memory().images == before.images,
                    "painting allocated hidden scratch instead of using reserved planes");
        const auto done = ctx.submit(cmd);
        cmd = {};
        workspace = {};
        test::check(ctx.memory().workspace == before.workspace,
                    "pending paint commands released their workspace");
        ctx.wait(done);
        test::check(ctx.memory().workspace == 0, "retired painting retained its workspace");
    });
    return test::finish();
}
