#include "../test.h"
#include "../../src/utils/workspace.h"

#include <cstdint>
#include <limits>

using namespace wgpupixel;
using enum ErrorCode;

// Expected sizes are written out by hand from the documented plan rules: slots are
// rounded up to 4 bytes and kept largest first, zero-byte requests take no slot,
// merge keeps the larger request per position, and bytes() is the sum of the slots.
int main() {
    test::run("plans round slots, skip empty requests and merge by slot", [] {
        test::check(WorkspacePlan{}.bytes() == 0, "default plan needs no memory");
        test::check(detail::workspace_plan({100, 3}).bytes() == 104, "3 bytes round up to 4");
        test::check(detail::workspace_plan({0, 8}).bytes() == 8, "zero-byte requests take no slot");

        auto a = detail::workspace_plan({100, 3});
        const auto b = detail::workspace_plan({50, 200, 8});
        auto ab = a;
        ab.merge(b);
        // {100, 4} and {200, 52, 8} merge position by position into {200, 52, 8}.
        test::check(b.bytes() == 200 + 52 + 8, "50 bytes round up to 52");
        test::check(ab.bytes() == 200 + 52 + 8, "merge pairs the largest requests together");
        auto ba = b;
        ba.merge(a);
        test::check(ba.bytes() == ab.bytes(), "merge is commutative");
        auto again = ab;
        again.merge(ab);
        test::check(again.bytes() == ab.bytes(), "merging a plan with itself changes nothing");
        a.merge(WorkspacePlan{});
        test::check(a.bytes() == 104, "merging an empty plan changes nothing");
    });

    test::run("plans whose size overflows are rejected", [] {
        const auto huge = std::numeric_limits<std::uint64_t>::max() - 2;
        bool rejected = false;
        try {
            (void)detail::workspace_plan({huge, 8}).bytes();
        } catch (const Error& error) {
            rejected = error.code() == capacity;
        }
        test::check(rejected, "a plan larger than 64-bit sizes must throw capacity");
    });

    test::run("workspaces are counted, aliased and released like other resources", [] {
        auto ctx = Context::create();
        const auto baseline = ctx.memory();
        test::check(baseline.workspace == 0, "no workspace memory before creation");

        auto empty = ctx.create_workspace(WorkspacePlan{});
        test::check(empty.capacity() == 0 && ctx.memory().workspace == 0,
                    "an empty plan creates a workspace without memory");

        const auto plan = detail::workspace_plan({1 << 20, 4096, 12});
        auto workspace = ctx.create_workspace(plan);
        test::check(workspace.capacity() == plan.bytes(), "capacity equals the plan");
        test::check(ctx.memory().workspace == plan.bytes(), "ledger counts workspace bytes");
        test::check(ctx.memory().total == baseline.total + plan.bytes(),
                    "workspace bytes are part of the total");

        auto alias = workspace;
        workspace = {};
        test::check(ctx.memory().workspace == plan.bytes(), "a live alias keeps the storage");
        alias = {};
        test::check(ctx.memory().workspace == 0 && ctx.memory().total == baseline.total,
                    "the last handle releases the storage");

        auto destroyed = ctx.create_workspace(plan);
        auto other = destroyed;
        ctx.destroy(destroyed);
        test::check(ctx.memory().workspace == 0, "destroy releases immediately");
        test::error(invalid_resource, "destroy", "workspace", [&] { ctx.destroy(other); });
    });

    test::run("workspace creation rolls back partial allocations at the memory limit", [] {
        auto ctx = Context::create();
        const auto before = ctx.memory().total;
        ctx.set_memory_limit(before + (1 << 20));
        test::error(capacity, "create_workspace", "memory_limit", [&] {
            (void)ctx.create_workspace(detail::workspace_plan({1 << 20, 1 << 20}));
        });
        test::check(ctx.memory().total == before && ctx.memory().workspace == 0,
                    "a rejected workspace must not keep any allocation");
        auto fits = ctx.create_workspace(detail::workspace_plan({1 << 19, 1 << 19}));
        test::check(ctx.memory().workspace == (1u << 20), "a plan within the limit is created");
    });

    test::run("workspaces outlive their context safely", [] {
        Workspace survivor;
        {
            auto ctx = Context::create();
            survivor = ctx.create_workspace(detail::workspace_plan({256}));
        }
        test::error(invalid_resource, "capacity", "resource", [&] { (void)survivor.capacity(); });
    });

    test::run("recording with a workspace allocates nothing and counts it once", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({300, 200}), result = ctx.create_image({300, 200});
        const MedianOptions options{.radius = 5};
        const auto requirements = median_requirements(image.size(), options);
        test::check(requirements.workspace.bytes() > 0, "radius 5 needs a workspace");
        auto workspace = ctx.create_workspace(requirements.workspace);
        const auto before = ctx.memory();
        test::check(before.workspace == requirements.workspace.bytes(),
                    "the workspace is counted once when created");
        for (int round = 0; round < 3; ++round) {
            auto cmd = ctx.create_commands();
            auto with = options;
            with.workspace = workspace;
            cmd.median(image, result, with);
            cmd.median(result, image, with); // Sequential calls reuse the same slots.
            const auto recorded = ctx.memory();
            test::check(recorded.workspace == before.workspace && recorded.images == before.images,
                        "recording must not allocate workspace or image storage");
            test::error(resource_busy, "destroy", "workspace", [&] { ctx.destroy(workspace); });
            ctx.submit_and_wait(cmd);
        }
        const auto after = ctx.memory();
        test::check(after.workspace == before.workspace && after.internal == before.internal,
                    "retired work must leave workspace and internal memory unchanged");
        ctx.destroy(workspace);
        test::check(ctx.memory().workspace == 0, "destroy releases the workspace after retirement");
    });

    return test::finish();
}
