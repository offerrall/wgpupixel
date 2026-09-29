#pragma once
#include "filter_pyramid.h"
#include <bit>

namespace wgpupixel::detail {
inline std::size_t median_commands(ImageSize size, std::int64_t radius) {
    if (radius <= 1) {
        return 1;
    }
    if (radius <= 3) {
        return filter_dispatches(size.width, size.height, 65536);
    }
    const auto width = std::uint32_t(std::min<std::int64_t>(1024, size.width) + 2 * radius);
    const unsigned bits = std::bit_width(width);
    return std::size_t((size.width + 1023) / 1024) * ((size.height + 1023) / 1024) * 4 *
           (17 + 32 * (bits + 1) * 3);
}
inline std::vector<Record> median_records(Operation& op, const std::shared_ptr<Resource>& source,
                                          const std::shared_ptr<Resource>& destination, int radius,
                                          const std::optional<Rect>& region) {
    std::vector<Record> records;
    const auto width = std::min(1024u, source->width) + 2 * radius;
    const auto height = std::min(1024u, source->height) + 2 * radius;
    const auto count = (width * height + 31u) / 32u * 32u;
    const auto words = count / 32u;
    const unsigned bits = std::bit_width(width);
    const auto planes_per_buffer = 8u * (bits + 1);
    auto temporary = [&](std::uint64_t bytes) {
        return op.temporary(std::uint32_t((bytes + 15) / 16), 1);
    };
    std::array<std::shared_ptr<Resource>, 4> matrix;
    for (auto& buffer : matrix) {
        buffer = temporary(std::uint64_t(planes_per_buffer) * words * 8);
    }
    auto totals = temporary(std::uint64_t((words + 63) / 64) * 4);
    auto values0 = temporary(std::uint64_t(count) * 8);
    auto values1 = temporary(std::uint64_t(count) * 8);
    auto coordinates0 = temporary(std::uint64_t(count + 1) * 4);
    auto coordinates1 = temporary(std::uint64_t(count + 1) * 4);
    const auto dispatch = [](Record& record, std::uint32_t invocations) {
        const auto groups = (invocations + 63u) / 64u;
        const auto columns = std::min(65535u, groups);
        record.parameters.reserved[3] = columns;
        record.parameters.dispatch = {0, 0, columns * 8, ((groups + columns - 1) / columns) * 8};
    };
    for (unsigned top = 0; top < source->height; top += 1024) {
        for (unsigned left = 0; left < source->width; left += 1024) {
            for (unsigned channel = 0; channel < 4; ++channel) {
                auto init = kernel_record(Kernel::median_wavelet_build, source, values0);
                init.sources[0] = matrix[0];
                init.sources[1] = coordinates0;
                init.sources[2] = totals;
                init.unmasked = init.starts_batch = true;
                init.parameters.offsets[2] = channel;
                init.parameters.reserved = {count, width, height, 0};
                init.parameters.color1 = {std::bit_cast<float>(left), std::bit_cast<float>(top),
                                          std::bit_cast<float>(radius), 0};
                dispatch(init, count);
                records.push_back(init);
                auto build = [&](const std::shared_ptr<Resource>& input,
                                 const std::shared_ptr<Resource>& output,
                                 const std::shared_ptr<Resource>& auxiliary, unsigned plane,
                                 unsigned bit, bool value) {
                    const auto buffer = matrix[plane / planes_per_buffer];
                    const auto offset = (plane % planes_per_buffer) * words;
                    auto pack = kernel_record(Kernel::median_wavelet_build, input, output);
                    pack.unmasked = true;
                    pack.starts_batch = value;
                    pack.sources[0] = buffer;
                    pack.sources[1] = auxiliary;
                    pack.sources[2] = totals;
                    pack.parameters.offsets = {value ? 1 : 3, int(bit), int(channel), int(offset)};
                    pack.parameters.reserved = {count, width, height, 0};
                    dispatch(pack, words);
                    records.push_back(pack);
                    auto scan = kernel_record(Kernel::median_wavelet_scan, buffer);
                    scan.unmasked = true;
                    scan.sources[0] = totals;
                    scan.parameters.offsets[3] = int(offset);
                    scan.parameters.reserved[0] = words;
                    scan.parameters.dispatch = {0, 0, 8, 8};
                    records.push_back(scan);
                    pack.starts_batch = false;
                    pack.parameters.offsets[0] = value ? 2 : 4;
                    dispatch(pack, count);
                    records.push_back(pack);
                };
                auto a = values0, b = values1;
                for (unsigned level = 0; level < 32; ++level) {
                    const auto plane = level * (bits + 1);
                    build(a, b, coordinates0, plane, 31 - level, true);
                    std::swap(a, b);
                    auto c = coordinates0, d = coordinates1;
                    for (unsigned xbit = 0; xbit < bits; ++xbit) {
                        build(c, d, a, plane + 1 + xbit, bits - 1 - xbit, false);
                        std::swap(c, d);
                    }
                }
                auto query = kernel_record(Kernel::median_wavelet_query, matrix[0], destination);
                query.sources = {matrix[1], matrix[2], matrix[3]};
                query.starts_batch = true;
                query.parameters.offsets = {radius, int(left), int(top), int(channel)};
                query.parameters.reserved = {width, height, words, planes_per_buffer};
                std::int64_t x0 = left, y0 = top, x1 = std::min(left + 1024, source->width),
                             y1 = std::min(top + 1024, source->height);
                if (region) {
                    x0 = std::max(x0, std::int64_t(region->x));
                    y0 = std::max(y0, std::int64_t(region->y));
                    x1 = std::min(x1, std::int64_t(region->x) + region->width);
                    y1 = std::min(y1, std::int64_t(region->y) + region->height);
                }
                query.parameters.dispatch = {unsigned(x0), unsigned(y0),
                                             unsigned(std::max<std::int64_t>(0, x1 - x0)),
                                             unsigned(std::max<std::int64_t>(0, y1 - y0))};
                const auto area = query.parameters.dispatch;
                for (unsigned y = 0; y < area[3]; y += 256) {
                    for (unsigned x = 0; x < area[2]; x += 256) {
                        auto tile = query;
                        tile.parameters.dispatch = {area[0] + x, area[1] + y,
                                                    std::min(256u, area[2] - x),
                                                    std::min(256u, area[3] - y)};
                        records.push_back(tile);
                    }
                }
            }
        }
    }
    return records;
}
} // namespace wgpupixel::detail
