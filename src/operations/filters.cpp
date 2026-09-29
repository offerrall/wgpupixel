#include "utils/operations.h"
#include "filter_sequence.h"
#include "filter_median.h"

namespace wgpupixel {
using namespace detail;
namespace {
void radius(const Operation& op, std::int64_t value, std::int64_t maximum) {
    op.require(value >= 0 && value <= maximum, "radius", "radius is outside the supported range");
}
void bounded(const Operation& op, float value, float maximum, std::string_view name) {
    op.require(std::isfinite(value) && value >= 0 && value <= maximum, name,
               "value must be finite and inside the supported range");
}
void pair(const Operation& op, const std::shared_ptr<Resource>& source,
          const std::shared_ptr<Resource>& destination) {
    op.resource(source, "source");
    op.resource(destination, "destination");
    op.distinct(*source, *destination);
    op.same_size(*source, *destination);
}
Record axis(const std::shared_ptr<Resource>& source, const std::shared_ptr<Resource>& destination,
            int radius, int direction, int mode) {
    auto record = kernel_record(Kernel::filter_axis, source, destination);
    record.parameters.offsets = {radius, direction, mode, 0};
    return record;
}
void weights(Operation& op, Record& first, Record& second, float sigma, int radius) {
    auto data = op.data(first, radius + 1, "radius");
    double sum = 1;
    for (int i = 1; i <= radius; ++i) {
        sum += 2 * std::exp(-0.5 * (double(i) / sigma) * (double(i) / sigma));
    }
    for (int i = 0; i <= radius; ++i) {
        const double ratio = sigma > 0 ? double(i) / sigma : 0;
        data[i] = std::bit_cast<std::uint32_t>(float(std::exp(-0.5 * ratio * ratio) / sum));
    }
    second.data_offset = first.data_offset;
    second.data_count = first.data_count;
}
} // namespace

void Commands::motion_blur(const Image& source, const Image& destination,
                           const MotionBlurOptions& options) {
    Operation op(recording_.get(), "motion_blur");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    pair(op, source.resource_, destination.resource_);
    auto record = kernel_record(Kernel::motion_blur, source.resource_, destination.resource_);
    bounded(op, options.distance, 4096, "distance");
    op.require(std::isfinite(options.angle) && std::abs(options.angle) <= 360, "angle",
               "angle must lie in [-360, 360]");
    op.workspace(
        options.workspace.storage_,
        motion_blur_requirements({source.resource_->width, source.resource_->height}, options)
            .workspace);
    const auto [sine, cosine] = detail::sin_cos_degrees(options.angle);
    if (options.distance > 32) {
        const int count = int(std::ceil(std::log2(2 * options.distance + 1)));
        const double step = options.distance / (std::ldexp(1., count) - 1);
        std::vector<Parameters> passes;
        for (int i = 0; i < count; ++i) {
            Parameters p{};
            p.values = {float(cosine * std::ldexp(step, i - 1)),
                        float(sine * std::ldexp(step, i - 1)), 0, 0};
            passes.push_back(p);
        }
        append_filter_records(op, filter_sequence(op, source.resource_, destination.resource_, passes),
                              options.region);
        return;
    }
    record.parameters.values = {float(cosine), float(sine), options.distance, 0};
    record.parameters.reserved[0] =
        std::max(1u, std::uint32_t(std::ceil(options.distance * 2)) + 1);
    op.append({record});
}

void Commands::radial_blur(const Image& source, const Image& destination,
                           const RadialBlurOptions& options) {
    Operation op(recording_.get(), "radial_blur");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    pair(op, source.resource_, destination.resource_);
    auto record = kernel_record(Kernel::radial_blur, source.resource_, destination.resource_);
    bounded(op, options.amount, 100, "amount");
    const Point center = options.center == Point{-1, -1} ? Point{source.resource_->width * 0.5f,
                                                                 source.resource_->height * 0.5f}
                                                         : options.center;
    op.require(std::isfinite(center.x) && std::isfinite(center.y) && center.x >= 0 &&
                   center.y >= 0 && center.x <= source.resource_->width &&
                   center.y <= source.resource_->height,
               "center", "center must be finite and inside the image");
    op.require(std::to_underlying(options.mode) <= 1, "mode", "radial blur mode is invalid");
    op.workspace(
        options.workspace.storage_,
        radial_blur_requirements({source.resource_->width, source.resource_->height}, options)
            .workspace);
    const double dx =
        std::max(std::abs(center.x - 0.5), std::abs(source.resource_->width - 0.5 - center.x));
    const double dy =
        std::max(std::abs(center.y - 0.5), std::abs(source.resource_->height - 0.5 - center.y));
    const double extent = std::hypot(dx, dy) * options.amount *
                          (options.mode == RadialBlurMode::spin ? 0.017453292519943295 : 0.01);
    if (extent > 32) {
        const double logarithm =
            -std::log(std::max(1. - options.amount / 100., 1. / (2 * std::hypot(dx, dy) + 1)));
        const bool spin = options.mode == RadialBlurMode::spin;
        const int count =
            int(std::ceil(std::log2(2 * (spin ? extent : logarithm * std::hypot(dx, dy)) + 1)));
        std::vector<Parameters> passes;
        for (int i = 0; i < count; ++i) {
            Parameters p{};
            p.color1 = {center.x, center.y, 0, 0};
            p.offsets[3] = spin ? 1 : 2;
            if (spin) {
                const double angle = options.amount * 0.017453292519943295 * std::ldexp(1., i - 1) /
                                     (std::ldexp(1., count) - 1);
                p.values = {float(std::cos(angle)), float(std::sin(angle)), 0, 0};
            } else {
                p.values[0] = float(std::exp(-std::ldexp(logarithm, i - count)));
            }
            passes.push_back(p);
        }
        append_filter_records(op, filter_sequence(op, source.resource_, destination.resource_, passes),
                              options.region);
        return;
    }
    record.parameters.values = {center.x, center.y, options.amount, 0};
    record.parameters.reserved[0] = std::to_underlying(options.mode);
    op.append({record});
}

void Commands::median(const Image& source, const Image& destination, const MedianOptions& options) {
    Operation op(recording_.get(), "median");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    pair(op, source.resource_, destination.resource_);
    auto record = kernel_record(Kernel::median, source.resource_, destination.resource_);
    radius(op, options.radius, 500);
    op.workspace(options.workspace.storage_,
                 median_requirements({source.resource_->width, source.resource_->height}, options).workspace);
    if (options.radius == 2 || options.radius == 3) {
        record.kernel = Kernel::median_small;
        record.parameters.offsets[0] = int(options.radius);
        op.reserve(
            filter_dispatches(source.resource_->width, source.resource_->height, 65536));
        append_filter_dispatches(op, record, 65536);
        return;
    }
    if (options.radius > 3) {
        op.reserve(
            median_commands({source.resource_->width, source.resource_->height}, options.radius));
        const auto records = median_records(op, source.resource_, destination.resource_,
                                            int(options.radius), options.region);
        append_filter_records(op, records, std::nullopt);
        return;
    }
    record.parameters.offsets[0] = static_cast<std::int32_t>(options.radius);
    op.append({record});
}

void Commands::pixelate(const Image& source, const Image& destination,
                        const PixelateOptions& options) {
    Operation op(recording_.get(), "pixelate");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    pair(op, source.resource_, destination.resource_);
    auto record = kernel_record(Kernel::pixelate, source.resource_, destination.resource_);
    op.require(options.cell_size >= 1 && options.cell_size <= 1024, "cell_size",
               "cell_size must lie in [1, 1024]");
    if (options.cell_size == 1) {
        op.append({kernel_record(Kernel::copy_image, source.resource_, destination.resource_)});
        return;
    }
    record.parameters.reserved[0] = static_cast<std::uint32_t>(options.cell_size);
    const auto width = source.resource_->width, height = source.resource_->height;
    std::int64_t left = 0, top = 0, right = width, bottom = height;
    if (options.region) {
        left = std::clamp<std::int64_t>(options.region->x, 0, width);
        top = std::clamp<std::int64_t>(options.region->y, 0, height);
        right = std::clamp<std::int64_t>(std::int64_t(options.region->x) + options.region->width, 0,
                                         width);
        bottom = std::clamp<std::int64_t>(std::int64_t(options.region->y) + options.region->height,
                                          0, height);
    }
    if (left >= right || top >= bottom) {
        return;
    }
    record.parameters.reserved[1] = static_cast<std::uint32_t>(left);
    record.parameters.reserved[2] = static_cast<std::uint32_t>(top);
    record.parameters.offsets[0] = static_cast<std::int32_t>(right);
    record.parameters.offsets[1] = static_cast<std::int32_t>(bottom);
    const auto cell = static_cast<std::uint32_t>(options.cell_size);
    const auto columns = (width + cell - 1) / cell;
    const auto cells = std::uint64_t(columns) * ((height + cell - 1) / cell);
    const auto groups = static_cast<std::uint32_t>(std::min<std::uint64_t>(cells, 65535));
    record.parameters.reserved[3] = columns;
    record.parameters.offsets[2] = static_cast<std::int32_t>(groups);
    record.parameters.dispatch = {0, 0, groups * 8,
                                  static_cast<std::uint32_t>((cells + groups - 1) / groups) * 8};
    op.region(nullptr);
    op.append({record});
}

void Commands::surface_blur(const Image& source, const Image& destination,
                            const SurfaceBlurOptions& options) {
    Operation op(recording_.get(), "surface_blur");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    pair(op, source.resource_, destination.resource_);
    auto record = kernel_record(Kernel::surface_blur, source.resource_, destination.resource_);
    radius(op, options.radius, 100);
    bounded(op, options.threshold, 255, "threshold");
    op.workspace(
        options.workspace.storage_,
        surface_blur_requirements({source.resource_->width, source.resource_->height}, options)
            .workspace);
    if (options.radius > 3 && options.threshold > 0) {
        std::vector<Parameters> passes;
        int support = 0;
        for (int step = 1; support < options.radius; step *= 2) {
            const int stride = std::min(step, int(options.radius) - support);
            for (int axis = 0; axis < 2; ++axis) {
                Parameters p{};
                p.offsets = {axis == 0 ? stride : 0, axis == 1 ? stride : 0, 0, 4};
                p.values[0] = options.threshold / 255;
                passes.push_back(p);
            }
            support += stride;
        }
        append_filter_records(op, filter_sequence(op, source.resource_, destination.resource_, passes),
                              options.region);
        return;
    }
    record.parameters.offsets[0] = static_cast<std::int32_t>(options.radius);
    record.parameters.values[0] = options.threshold / 255;
    op.append({record});
}

void Commands::box_blur(const Image& source, const Image& destination,
                        const BoxBlurOptions& options) {
    Operation op(recording_.get(), "box_blur");
    pair(op, source.resource_, destination.resource_);
    radius(op, options.radius, 1024);
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    op.workspace(options.workspace.storage_,
                 box_blur_requirements({source.resource_->width, source.resource_->height}, options)
                     .workspace);
    auto scratch = op.temporary(source.resource_->width, source.resource_->height);
    auto first = kernel_record(Kernel::box_axis, source.resource_, scratch);
    auto second = kernel_record(Kernel::box_axis, scratch, destination.resource_);
    first.parameters.offsets = {static_cast<int>(options.radius), 0, 0, 0};
    second.parameters.offsets = {static_cast<int>(options.radius), 1, 0, 0};
    const auto w = source.resource_->width, h = source.resource_->height;
    first.parameters.dispatch = {0, 0, (w + 127) / 128, h};
    second.parameters.dispatch = {0, 0, w, (h + 127) / 128};
    second.parameters.reserved = {0, 0, w, h};
    if (options.region) {
        const auto area = *options.region;
        second.parameters.reserved = {
            static_cast<std::uint32_t>(std::clamp<std::int64_t>(area.x, 0, w)),
            static_cast<std::uint32_t>(std::clamp<std::int64_t>(area.y, 0, h)),
            static_cast<std::uint32_t>(
                std::clamp<std::int64_t>(std::int64_t(area.x) + area.width, 0, w)),
            static_cast<std::uint32_t>(
                std::clamp<std::int64_t>(std::int64_t(area.y) + area.height, 0, h))};
    }
    if (second.parameters.reserved[0] >= second.parameters.reserved[2] ||
        second.parameters.reserved[1] >= second.parameters.reserved[3]) {
        return;
    }
    op.region(nullptr);
    op.reserve(
        filter_dispatches(first.parameters.dispatch[2], first.parameters.dispatch[3], 4096) +
        filter_dispatches(second.parameters.dispatch[2], second.parameters.dispatch[3], 4096));
    append_filter_dispatches(op, first, 4096);
    append_filter_dispatches(op, second, 4096);
}

void Commands::minimum(const Image& source, const Image& destination,
                       const MorphologyOptions& options) {
    Operation op(recording_.get(), "minimum");
    pair(op, source.resource_, destination.resource_);
    radius(op, options.radius, 500);
    op.require(std::to_underlying(options.shape) <= 1, "shape", "morphology shape is invalid");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    op.workspace(options.workspace.storage_,
                 minimum_requirements({source.resource_->width, source.resource_->height}, options)
                     .workspace);
    if (options.shape == MorphologyShape::round && options.radius <= 10) {
        auto record =
            kernel_record(Kernel::morphology_round, source.resource_, destination.resource_);
        record.parameters.offsets = {static_cast<std::int32_t>(options.radius), 0, 1, 0};
        op.reserve(
            filter_dispatches(source.resource_->width, source.resource_->height, 65536));
        append_filter_dispatches(op, record, 65536);
        return;
    }
    const auto passes = extrema_passes(int(options.radius), options.shape, 1);
    append_filter_records(op, filter_sequence(op, source.resource_, destination.resource_, passes),
                          options.region, options.shape == MorphologyShape::round);
}

void Commands::maximum(const Image& source, const Image& destination,
                       const MorphologyOptions& options) {
    Operation op(recording_.get(), "maximum");
    pair(op, source.resource_, destination.resource_);
    radius(op, options.radius, 500);
    op.require(std::to_underlying(options.shape) <= 1, "shape", "morphology shape is invalid");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    op.workspace(options.workspace.storage_,
                 maximum_requirements({source.resource_->width, source.resource_->height}, options)
                     .workspace);
    if (options.shape == MorphologyShape::round && options.radius <= 10) {
        auto record =
            kernel_record(Kernel::morphology_round, source.resource_, destination.resource_);
        record.parameters.offsets = {static_cast<std::int32_t>(options.radius), 0, 2, 0};
        op.reserve(
            filter_dispatches(source.resource_->width, source.resource_->height, 65536));
        append_filter_dispatches(op, record, 65536);
        return;
    }
    const auto passes = extrema_passes(int(options.radius), options.shape, 2);
    append_filter_records(op, filter_sequence(op, source.resource_, destination.resource_, passes),
                          options.region, options.shape == MorphologyShape::round);
}

void Commands::unsharp_mask(const Image& source, const Image& destination,
                            const UnsharpMaskOptions& options) {
    Operation op(recording_.get(), "unsharp_mask");
    pair(op, source.resource_, destination.resource_);
    bounded(op, options.radius, 1000, "radius");
    bounded(op, options.amount, 500, "amount");
    bounded(op, options.threshold, 255, "threshold");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    op.workspace(
        options.workspace.storage_,
        unsharp_mask_requirements({source.resource_->width, source.resource_->height}, options)
            .workspace);
    auto blurred = op.temporary(source.resource_->width, source.resource_->height);
    if (options.radius > 32) {
        auto records = large_gaussian(op, source.resource_, blurred, options.radius,
                                      std::ceil(3 * options.radius));
        records.back().parameters.values[1] = 1;
        auto finish = kernel_record(Kernel::filter_detail, source.resource_, destination.resource_);
        finish.sources[0] = blurred;
        finish.parameters.values = {options.amount / 100, options.threshold / 255, 0, 0};
        records.push_back(finish);
        append_filter_records(op, records, options.region);
        return;
    }
    auto scratch = op.temporary(source.resource_->width, source.resource_->height);
    const int support = static_cast<int>(std::ceil(3 * options.radius));
    auto first = axis(source.resource_, scratch, support, 0, 3);
    auto second = axis(scratch, blurred, support, 1, 3);
    weights(op, first, second, options.radius, support);
    auto finish = kernel_record(Kernel::filter_detail, source.resource_, destination.resource_);
    finish.sources[0] = blurred;
    finish.parameters.values = {options.amount / 100, options.threshold / 255, 0, 0};
    op.reserve(3);
    op.region(nullptr);
    op.append({first, second});
    op.region(options.region ? &*options.region : nullptr);
    op.append({finish});
}

void Commands::high_pass(const Image& source, const Image& destination,
                         const HighPassOptions& options) {
    Operation op(recording_.get(), "high_pass");
    pair(op, source.resource_, destination.resource_);
    bounded(op, options.radius, 1000, "radius");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    op.workspace(
        options.workspace.storage_,
        high_pass_requirements({source.resource_->width, source.resource_->height}, options)
            .workspace);
    auto blurred = op.temporary(source.resource_->width, source.resource_->height);
    if (options.radius > 32) {
        auto records = large_gaussian(op, source.resource_, blurred, options.radius,
                                      std::ceil(3 * options.radius));
        records.back().parameters.values[1] = 1;
        auto finish = kernel_record(Kernel::filter_detail, source.resource_, destination.resource_);
        finish.sources[0] = blurred;
        finish.parameters.values[2] = 1;
        records.push_back(finish);
        append_filter_records(op, records, options.region);
        return;
    }
    auto scratch = op.temporary(source.resource_->width, source.resource_->height);
    const int support = static_cast<int>(std::ceil(3 * options.radius));
    auto first = axis(source.resource_, scratch, support, 0, 3);
    auto second = axis(scratch, blurred, support, 1, 3);
    weights(op, first, second, options.radius, support);
    auto finish = kernel_record(Kernel::filter_detail, source.resource_, destination.resource_);
    finish.sources[0] = blurred;
    finish.parameters.values[2] = 1;
    op.reserve(3);
    op.region(nullptr);
    op.append({first, second});
    op.region(options.region ? &*options.region : nullptr);
    op.append({finish});
}

void Commands::add_noise(const Image& destination, const AddNoiseOptions& options) {
    Operation op(recording_.get(), "add_noise");
    op.selection(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                 destination.resource_, options.mask != nullptr, options.region);
    auto pixels = op.resource(destination.resource_, "destination");
    bounded(op, options.amount, 400, "amount");
    op.require(std::to_underlying(options.distribution) <= 1, "distribution",
               "noise distribution is invalid");
    auto record = kernel_record(Kernel::add_noise, pixels);
    record.parameters.values[0] = options.amount / 100;
    record.parameters.reserved = {options.seed, options.monochrome ? 1u : 0u,
                                  std::to_underlying(options.distribution), options.clip ? 1u : 0u};
    op.append({record});
}
} // namespace wgpupixel
