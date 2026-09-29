#pragma once
#include "filter_pyramid.h"

namespace wgpupixel::detail {
inline std::vector<Record> filter_sequence(Operation& op, const std::shared_ptr<Resource>& source,
                                           const std::shared_ptr<Resource>& destination,
                                           const std::vector<Parameters>& passes,
                                           std::shared_ptr<Resource> scratch = {}) {
    std::vector<Record> records;
    if (!scratch) {
        scratch = filter_temporary(op, source->width, source->height);
    }
    auto second = passes.size() > 2 ? filter_temporary(op, source->width, source->height) : scratch;
    auto input = source;
    for (std::size_t i = 0; i < passes.size(); ++i) {
        auto output = i + 1 == passes.size() ? destination : (i % 2 == 0 ? scratch : second);
        auto record = kernel_record(Kernel::filter_sequence, input, output);
        record.sources[0] = source;
        record.starts_batch = i % 8 == 0;
        record.parameters.values = passes[i].values;
        record.parameters.offsets = passes[i].offsets;
        record.parameters.color1 = passes[i].color1;
        record.parameters.reserved[0] = i + 1 == passes.size() ? 1 : 0;
        records.push_back(record);
        input = output;
    }
    return records;
}
inline void extrema_line(std::vector<Parameters>& passes, int radius, int x, int y, int mode) {
    int support = 0;
    do {
        const auto step = std::min(2 * support + 1, radius - support);
        Parameters parameters{};
        parameters.offsets = {x * step, y * step, mode, 3};
        passes.push_back(parameters);
        support += step;
    } while (support < radius);
}
inline std::vector<Parameters> extrema_passes(int radius, MorphologyShape shape, int mode) {
    std::vector<Parameters> passes;
    if (shape == MorphologyShape::square) {
        extrema_line(passes, radius, 1, 0, mode);
        extrema_line(passes, radius, 0, 1, mode);
    } else {
        constexpr std::array<std::array<int, 2>, 8> directions{
            {{1, 0}, {0, 1}, {1, 1}, {1, -1}, {2, 1}, {2, -1}, {1, 2}, {1, -2}}};
        const double normalization = 1 + std::sqrt(2.) + 6 / std::sqrt(5.);
        for (const auto& d : directions) {
            const int length = int(std::round(radius / (normalization * std::hypot(d[0], d[1]))));
            if (length) {
                extrema_line(passes, length, d[0], d[1], mode);
            }
        }
    }
    return passes;
}
} // namespace wgpupixel::detail
