#pragma once
#include "filter_weights.h"
#include "filter_dispatch.h"
#include "utils/workspace.h"

namespace wgpupixel::detail {
inline std::shared_ptr<Resource> filter_temporary(Operation& op, std::uint32_t width,
                                                  std::uint32_t height) {
    return op.temporary(width, height);
}
inline unsigned filter_levels(ImageSize size, double support) {
    unsigned levels = 0;
    while (support > 64 && (size.width > 2 || size.height > 2)) {
        size = {std::min(size.width, std::max<std::int64_t>(2, (size.width + 1) / 2)),
                std::min(size.height, std::max<std::int64_t>(2, (size.height + 1) / 2))};
        support *= .5;
        ++levels;
    }
    return levels;
}
// Each pyramid level and both final working planes need independent storage.
inline WorkspacePlan gaussian_workspace(ImageSize size, double support, std::string_view name) {
    WorkspacePlan plan;
    const auto levels = filter_levels(size, support);
    for (unsigned i = 0; i < levels; ++i) {
        size = {std::min(size.width, std::max<std::int64_t>(2, (size.width + 1) / 2)),
                std::min(size.height, std::max<std::int64_t>(2, (size.height + 1) / 2))};
        workspace_buffer(plan, std::uint64_t(size.width) * size.height * pixel_bytes, name);
    }
    for (int i = 0; i < 2; ++i) {
        workspace_buffer(plan, std::uint64_t(size.width) * size.height * pixel_bytes, name);
    }
    return plan;
}
inline std::vector<Record> filter_pyramid(Operation& op, const std::shared_ptr<Resource>& source,
                                          unsigned levels) {
    std::vector<Record> records;
    auto input = source;
    for (unsigned i = 0; i < levels; ++i) {
        auto output =
            filter_temporary(op, std::min(input->width, std::max(2u, (input->width + 1) / 2)),
                             std::min(input->height, std::max(2u, (input->height + 1) / 2)));
        auto record = kernel_record(Kernel::filter_reduce, input, output);
        record.parameters.offsets[0] = 3;
        records.push_back(record);
        input = output;
    }
    return records;
}
inline void append_filter_records(Operation& op, const std::vector<Record>& records,
                                  const std::optional<Rect>& region, bool tiled = false) {
    std::size_t count = 0;
    for (const auto& record : records) {
        count +=
            tiled ? filter_dispatches(record.parameters.dispatch[2], record.parameters.dispatch[3])
                  : 1;
    }
    op.reserve(count);
    op.region(nullptr);
    for (std::size_t i = 0; i < records.size(); ++i) {
        if (i + 1 == records.size()) {
            op.region(region ? &*region : nullptr);
        }
        auto record = records[i];
        record.unmasked = record.unmasked || record.kernel == Kernel::filter_reduce;
        if (tiled) {
            append_filter_dispatches(op, record);
        } else {
            op.append({record});
        }
    }
}
inline std::vector<Record> large_gaussian(Operation& op, const std::shared_ptr<Resource>& source,
                                          const std::shared_ptr<Resource>& destination,
                                          double sigma, double radius) {
    const auto levels =
        filter_levels({source->width, source->height}, std::min(radius, std::floor(sigma * 16)));
    auto records = filter_pyramid(op, source, levels);
    auto input = records.empty() ? source : records.back().destination;
    auto scratch = filter_temporary(op, input->width, input->height);
    auto blurred = filter_temporary(op, input->width, input->height);
    const double scale_x = double(std::max(1u, source->width - 1)) / std::max(1u, input->width - 1);
    const double scale_y =
        double(std::max(1u, source->height - 1)) / std::max(1u, input->height - 1);
    const int support_x = int(std::min(64., std::ceil(radius / scale_x)));
    const int support_y = int(std::min(64., std::ceil(radius / scale_y)));
    auto horizontal = kernel_record(Kernel::filter_axis, input, scratch);
    auto vertical = kernel_record(Kernel::filter_axis, scratch, blurred);
    horizontal.parameters.offsets = {support_x, 0, 3, 0};
    vertical.parameters.offsets = {support_y, 1, 3, 0};
    Record unused;
    filter_weights(op, horizontal, unused, float(sigma / scale_x), support_x);
    filter_weights(op, vertical, unused, float(sigma / scale_y), support_y);
    horizontal.parameters.values[3] = vertical.parameters.values[3] = 2;
    records.push_back(horizontal);
    records.push_back(vertical);
    auto expand = kernel_record(Kernel::filter_expand, blurred, destination);
    expand.parameters.values[0] = 1;
    expand.parameters.values[2] = 1;
    records.push_back(expand);
    return records;
}
} // namespace wgpupixel::detail
