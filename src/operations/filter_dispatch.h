#pragma once
#include "utils/operations.h"

namespace wgpupixel::detail {
inline std::size_t filter_dispatches(std::uint32_t width, std::uint32_t height,
                                     std::uint32_t budget = 1048576) {
    if (width == 0 || height == 0) {
        return 0;
    }
    const auto columns = std::min(256u, width);
    const auto rows = std::max(8u, budget / std::max(8u, columns) / 8 * 8);
    return std::size_t((width + columns - 1) / columns) * ((height + rows - 1) / rows);
}
inline void append_filter_dispatches(Operation& op, const Record& record,
                                     std::uint32_t budget = 1048576) {
    const auto area = record.parameters.dispatch;
    const auto columns = std::min(256u, area[2]);
    const auto rows = std::max(8u, budget / std::max(8u, columns) / 8 * 8);
    for (unsigned y = 0; y < area[3]; y += rows) {
        for (unsigned x = 0; x < area[2]; x += columns) {
            auto tile = record;
            tile.starts_batch = true;
            tile.parameters.dispatch = {area[0] + x, area[1] + y, std::min(columns, area[2] - x),
                                        std::min(rows, area[3] - y)};
            op.append({tile});
        }
    }
}
} // namespace wgpupixel::detail
