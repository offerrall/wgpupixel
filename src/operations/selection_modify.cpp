#include "selection.h"
#include <limits>

namespace wgpupixel {
using namespace detail;
namespace {
constexpr auto maximum = std::numeric_limits<std::int32_t>::max();
constexpr float feather_limit = 1000.0f;
// Keeps packed squared distances (2 (radius + 2)^2 << 8) within 32 bits.
constexpr float distance_limit = 2000.0f;
constexpr std::int64_t smooth_limit = 100;

void check(bool condition, std::string_view operation, std::string_view parameter,
           std::string_view message) {
    if (!condition) {
        fail(ErrorCode::invalid_argument, operation, parameter, message);
    }
}
void check_size(ImageSize size, std::string_view operation) {
    check(size.width > 0 && size.height > 0 && size.width <= maximum && size.height <= maximum,
          operation, "mask", "mask dimensions must be positive 32-bit signed integers");
    // Checked before any scratch arithmetic, which then stays far below 64-bit limits.
    if (size.width * size.height > std::int64_t{std::numeric_limits<std::uint32_t>::max()}) {
        fail(ErrorCode::capacity, operation, "mask", "pixel count exceeds shader address capacity");
    }
}
ImageSize scratch_rows(ImageSize mask, std::int64_t bytes, std::string_view operation) {
    const auto height = (bytes + mask.width - 1) / mask.width;
    check(height <= maximum, operation, "mask", "scratch dimensions exceed supported range");
    return {mask.width, height};
}
ImageSize filter_scratch(ImageSize mask, std::string_view operation) {
    const auto pixels = mask.width * mask.height;
    return scratch_rows(mask, 4 * ((pixels + 3) / 4 + (pixels + 1) / 2), operation);
}
ImageSize distance_scratch(ImageSize mask, std::int64_t planes, std::string_view operation) {
    return scratch_rows(mask, 4 * planes * mask.width * mask.height, operation);
}
bool valid_radius(float value, float limit) {
    return std::isfinite(value) && value >= 0 && value <= limit;
}
std::uint32_t distance_cap(float radius) {
    return static_cast<std::uint32_t>(std::ceil(radius)) + 2;
}

// Exact distance transform of the pixels at or above 50% coverage (inside) or below
// it, capped at cap pixels, into scratch plane `plane`.
std::array<Record, 3> distance_records(const std::shared_ptr<Resource>& mask,
                                       const std::shared_ptr<Resource>& scratch, bool outside,
                                       std::uint32_t cap, std::uint32_t plane) {
    const std::uint64_t width = mask->width, height = mask->height;
    auto columns = kernel_record(Kernel::mask_distance_columns, mask, scratch);
    columns.parameters.reserved = {outside ? 1u : 0u, cap, 0, 0};
    columns.parameters.dispatch = linear_dispatch(width * ((height + 63) / 64));
    auto segments = kernel_record(Kernel::mask_distance_segments, scratch);
    segments.parameters.offsets = {static_cast<std::int32_t>(width),
                                   static_cast<std::int32_t>(height), 0, 0};
    segments.parameters.dispatch = linear_dispatch(height * ((width + 15) / 16));
    auto rows = kernel_record(Kernel::mask_distance_rows, scratch);
    rows.parameters.offsets = segments.parameters.offsets;
    rows.parameters.reserved = {plane, cap, 0, 0};
    rows.parameters.dispatch = linear_dispatch(width * height);
    return {columns, segments, rows};
}
// The coverage pass reads the original bytes from scratch word 0, freed by then.
Record original_copy(const std::shared_ptr<Resource>& mask,
                     const std::shared_ptr<Resource>& scratch) {
    auto copy = copy_record(mask, scratch);
    copy.bytes = std::uint64_t(mask->width) * mask->height;
    return copy;
}
Record distance_coverage(const std::shared_ptr<Resource>& mask,
                         const std::shared_ptr<Resource>& scratch, std::uint32_t operation,
                         float radius, bool canvas_bounds) {
    auto record = kernel_record(Kernel::mask_distance_coverage, scratch, mask);
    record.parameters.reserved = {operation, canvas_bounds ? 1u : 0u, 0, 0};
    record.parameters.values[0] = radius;
    record.parameters.dispatch = linear_dispatch(mask_words(*mask));
    return record;
}
void record_lut(Operation& op, const std::shared_ptr<Resource>& mask,
                const std::array<std::uint8_t, 256>& table,
                const std::shared_ptr<Resource>& selection, const std::optional<Rect>& region) {
    auto record = kernel_record(Kernel::mask_lut, mask);
    record.parameters.dispatch = linear_dispatch(mask_words(*mask));
    mask_write(op, record, selection, region);
    auto words = op.data(record, 64, "mask");
    for (std::size_t i = 0; i < 64; ++i) {
        words[i] = std::uint32_t(table[4 * i]) | std::uint32_t(table[4 * i + 1]) << 8 |
                   std::uint32_t(table[4 * i + 2]) << 16 | std::uint32_t(table[4 * i + 3]) << 24;
    }
    op.append({record});
}
} // namespace

// Feather: coverage bytes, then the 16-bit sums of the exact filter or the two float
// planes of the bordered reduced grid (at most (width + 5) (height + 5) / 2 words).
ImageSize detail::feather_scratch(ImageSize mask, std::string_view operation) {
    const auto pixels = mask.width * mask.height;
    return scratch_rows(
        mask, 4 * ((pixels + 3) / 4 + (pixels + 1) / 2 + 3 * (mask.width + mask.height) + 16),
        operation);
}

std::vector<float> detail::feather_weights(double radius) {
    const auto taps = static_cast<std::size_t>(std::ceil(3 * radius));
    std::vector<double> exact(taps + 1);
    double total = 0;
    for (std::size_t i = 0; i <= taps; ++i) {
        const double ratio = double(i) / radius;
        exact[i] = std::exp(-0.5 * ratio * ratio);
        total += i == 0 ? exact[i] : 2 * exact[i];
    }
    std::vector<float> weights(exact.size());
    std::ranges::transform(exact, weights.begin(), [&](double w) { return float(w / total); });
    return weights;
}

std::array<Record, 2>
detail::mask_filter_records(Operation& op, const std::shared_ptr<Resource>& scratch,
                            const std::shared_ptr<Resource>& destination, std::uint32_t kind,
                            std::span<const float> weights, std::uint32_t radius,
                            bool canvas_bounds, SelectionMode mode) {
    const auto size = size_of(*destination);
    const std::array<std::uint32_t, 4> reserved{kind, radius, canvas_bounds ? 1u : 0u,
                                                filter_sums_offset(size)};
    auto horizontal = kernel_record(Kernel::mask_filter_horizontal, scratch);
    horizontal.parameters.offsets = {static_cast<std::int32_t>(size.width),
                                     static_cast<std::int32_t>(size.height), 0, 0};
    horizontal.parameters.reserved = reserved;
    horizontal.parameters.dispatch = linear_dispatch((size.width * size.height + 1) / 2);
    op.data(horizontal, weights, "radius");
    auto vertical = kernel_record(Kernel::mask_filter_vertical, scratch, destination);
    vertical.parameters.offsets = horizontal.parameters.offsets;
    vertical.parameters.offsets[2] = static_cast<std::int32_t>(std::to_underlying(mode));
    vertical.parameters.reserved = reserved;
    vertical.parameters.dispatch = linear_dispatch(mask_words(*destination));
    op.data(vertical, weights, "radius");
    return {horizontal, vertical};
}

std::size_t detail::feather_passes(double radius) {
    return std::ceil(3 * radius) <= fir_taps ? 2 : 4;
}

std::vector<Record> detail::feather_records(Operation& op, const std::shared_ptr<Resource>& scratch,
                                            const std::shared_ptr<Resource>& destination,
                                            double radius, bool canvas_bounds, SelectionMode mode) {
    if (feather_passes(radius) == 2) {
        const auto weights = feather_weights(radius);
        const auto passes =
            mask_filter_records(op, scratch, destination, 0, weights,
                                std::uint32_t(weights.size() - 1), canvas_bounds, mode);
        return {passes[0], passes[1]};
    }
    // Large radii: block means on a grid reduced by a power of two, an exact Gaussian of
    // at most 48 taps there, and bilinear reconstruction; O(1) work per pixel. The
    // coarse radius removes the variance the block means and the reconstruction add.
    std::uint32_t factor = 2;
    while (radius / factor > fir_taps / 3) {
        factor *= 2;
    }
    const double f = factor;
    const double coarse = std::sqrt(radius * radius - (f * f - 1) / 12 - f * f / 6) / f;
    const auto weights = feather_weights(coarse);
    const auto size = size_of(*destination);
    // One border cell on each side carries the canvas edge (mask_downsample.wgsl).
    const auto columns = (size.width + factor - 1) / factor + 2;
    const auto rows = (size.height + factor - 1) / factor + 2;
    const auto first = filter_sums_offset(size);
    const auto second = first + std::uint32_t(columns * rows);
    const std::uint32_t bounds = canvas_bounds ? 1 : 0;
    const std::array<std::int32_t, 4> fine{static_cast<std::int32_t>(size.width),
                                           static_cast<std::int32_t>(size.height),
                                           static_cast<std::int32_t>(factor), 0};
    auto reduce = kernel_record(Kernel::mask_downsample, scratch);
    reduce.parameters.offsets = fine;
    reduce.parameters.reserved = {bounds, 0, 0, 0};
    reduce.parameters.values = {std::bit_cast<float>(first), 0, 0, 0};
    reduce.parameters.dispatch = linear_dispatch(std::uint64_t(columns * rows));
    std::array<Record, 2> blur;
    for (std::uint32_t axis = 0; axis < 2; ++axis) {
        blur[axis] = kernel_record(Kernel::mask_coarse_blur, scratch);
        blur[axis].parameters.offsets = {static_cast<std::int32_t>(columns),
                                         static_cast<std::int32_t>(rows), 0, 0};
        blur[axis].parameters.reserved = {axis, std::uint32_t(weights.size() - 1), bounds, 0};
        blur[axis].parameters.values = {std::bit_cast<float>(axis == 0 ? first : second),
                                        std::bit_cast<float>(axis == 0 ? second : first), 0, 0};
        blur[axis].parameters.dispatch = linear_dispatch(std::uint64_t(columns * rows));
        op.data(blur[axis], std::span<const float>(weights), "radius");
    }
    auto expand = kernel_record(Kernel::mask_upsample, scratch, destination);
    expand.parameters.offsets = fine;
    expand.parameters.reserved = {std::to_underlying(mode), 0, 0, 0};
    expand.parameters.values = {std::bit_cast<float>(first), 0, 0, 0};
    expand.parameters.dispatch = linear_dispatch(mask_words(*destination));
    return {reduce, blur[0], blur[1], expand};
}

OperationRequirements feather_requirements(ImageSize mask, const FeatherOptions& options) {
    constexpr std::string_view operation = "feather_requirements";
    check(valid_radius(options.radius, feather_limit), operation, "radius",
          "radius must be finite and in [0, 1000]");
    check_size(mask, operation);
    return {mask, options.radius == 0 ? WorkspacePlan{}
                                      : scratch_plan(feather_scratch(mask, operation), operation)};
}

OperationRequirements smooth_requirements(ImageSize mask, const SmoothOptions& options) {
    constexpr std::string_view operation = "smooth_requirements";
    check(options.radius >= 0 && options.radius <= smooth_limit, operation, "radius",
          "radius must be in [0, 100]");
    check_size(mask, operation);
    return {mask, options.radius == 0 ? WorkspacePlan{}
                                      : scratch_plan(filter_scratch(mask, operation), operation)};
}

OperationRequirements expand_requirements(ImageSize mask, const ExpandOptions& options) {
    constexpr std::string_view operation = "expand_requirements";
    check(valid_radius(options.radius, distance_limit), operation, "radius",
          "radius must be finite and in [0, 2000]");
    check_size(mask, operation);
    return {mask, options.radius == 0
                      ? WorkspacePlan{}
                      : scratch_plan(distance_scratch(mask, 3, operation), operation)};
}

OperationRequirements contract_requirements(ImageSize mask, const ContractOptions& options) {
    constexpr std::string_view operation = "contract_requirements";
    check(valid_radius(options.radius, distance_limit), operation, "radius",
          "radius must be finite and in [0, 2000]");
    check_size(mask, operation);
    return {mask, options.radius == 0
                      ? WorkspacePlan{}
                      : scratch_plan(distance_scratch(mask, 3, operation), operation)};
}

OperationRequirements border_requirements(ImageSize mask, const BorderOptions& options) {
    constexpr std::string_view operation = "border_requirements";
    check(valid_radius(options.width, 2 * distance_limit) && options.width > 0, operation, "width",
          "width must be finite and in (0, 4000]");
    check_size(mask, operation);
    return {mask, scratch_plan(distance_scratch(mask, 4, operation), operation)};
}

void Commands::combine(const Mask& source, const Mask& destination,
                       const MaskCombineOptions& options) {
    Operation op(recording_.get(), "combine");
    const auto& src = op.resource(source.resource_, "source", ResourceKind::mask);
    const auto& dst = op.resource(destination.resource_, "destination", ResourceKind::mask);
    op.distinct(*src, *dst);
    op.same_size(*src, *dst);
    require_mode(op, options.mode);
    auto record = kernel_record(Kernel::mask_combine, src, dst);
    record.parameters.reserved[0] = std::to_underlying(options.mode);
    record.parameters.dispatch = linear_dispatch(mask_words(*dst));
    mask_write(op, record,
               options.mask ? op.resource(options.mask->resource_, "mask", ResourceKind::mask)
                            : nullptr,
               options.region);
    op.append({record});
}

void Commands::feather(const Mask& mask, const FeatherOptions& options) {
    Operation op(recording_.get(), "feather");
    const auto& target = op.resource(mask.resource_, "mask", ResourceKind::mask);
    op.require(valid_radius(options.radius, feather_limit), "radius",
               "radius must be finite and in [0, 1000]");
    const auto work = scratch_view(op, options.workspace.storage_,
                                   options.radius == 0 ? ImageSize{}
                                                       : feather_scratch(size_of(*target), "feather"));
    if (!work) {
        return;
    }
    auto copy = copy_record(target, work);
    copy.bytes = std::uint64_t(target->width) * target->height;
    const auto passes = feather_records(op, work, target, options.radius, options.canvas_bounds,
                                        SelectionMode::replace);
    op.reserve(1 + passes.size());
    op.append({copy});
    for (const auto& pass : passes) {
        op.append({pass});
    }
}

void Commands::smooth(const Mask& mask, const SmoothOptions& options) {
    Operation op(recording_.get(), "smooth");
    const auto& target = op.resource(mask.resource_, "mask", ResourceKind::mask);
    op.require(options.radius >= 0 && options.radius <= smooth_limit, "radius",
               "radius must be in [0, 100]");
    const auto work = scratch_view(op, options.workspace.storage_,
                                   options.radius == 0 ? ImageSize{}
                                                       : filter_scratch(size_of(*target), "smooth"));
    if (!work) {
        return;
    }
    auto copy = copy_record(target, work);
    copy.bytes = std::uint64_t(target->width) * target->height;
    const auto passes = mask_filter_records(op, work, target, 1, {}, std::uint32_t(options.radius),
                                            options.canvas_bounds, SelectionMode::replace);
    op.append({copy, passes[0], passes[1]});
}

void Commands::expand(const Mask& mask, const ExpandOptions& options) {
    Operation op(recording_.get(), "expand");
    const auto& target = op.resource(mask.resource_, "mask", ResourceKind::mask);
    op.require(valid_radius(options.radius, distance_limit), "radius",
               "radius must be finite and in [0, 2000]");
    const auto work =
        scratch_view(op, options.workspace.storage_,
                     options.radius == 0 ? ImageSize{} : distance_scratch(size_of(*target), 3, "expand"));
    if (!work) {
        return;
    }
    const auto passes = distance_records(target, work, false, distance_cap(options.radius), 2);
    op.append({passes[0], passes[1], passes[2], original_copy(target, work),
               distance_coverage(target, work, 0, options.radius, false)});
}

void Commands::contract(const Mask& mask, const ContractOptions& options) {
    Operation op(recording_.get(), "contract");
    const auto& target = op.resource(mask.resource_, "mask", ResourceKind::mask);
    op.require(valid_radius(options.radius, distance_limit), "radius",
               "radius must be finite and in [0, 2000]");
    const auto work = scratch_view(
        op, options.workspace.storage_,
        options.radius == 0 ? ImageSize{} : distance_scratch(size_of(*target), 3, "contract"));
    if (!work) {
        return;
    }
    const auto passes = distance_records(target, work, true, distance_cap(options.radius), 2);
    op.append({passes[0], passes[1], passes[2], original_copy(target, work),
               distance_coverage(target, work, 1, options.radius, options.canvas_bounds)});
}

void Commands::border(const Mask& mask, const BorderOptions& options) {
    Operation op(recording_.get(), "border");
    const auto& target = op.resource(mask.resource_, "mask", ResourceKind::mask);
    op.require(valid_radius(options.width, 2 * distance_limit) && options.width > 0, "width",
               "width must be finite and in (0, 4000]");
    const auto work =
        scratch_view(op, options.workspace.storage_, distance_scratch(size_of(*target), 4, "border"));
    const float radius = options.width / 2;
    const auto cap = distance_cap(radius);
    const auto outside = distance_records(target, work, true, cap, 3);
    const auto inside = distance_records(target, work, false, cap, 2);
    op.append({outside[0], outside[1], outside[2], inside[0], inside[1], inside[2],
               original_copy(target, work),
               distance_coverage(target, work, 2, radius, options.canvas_bounds)});
}

void Commands::threshold(const Mask& mask, const MaskThresholdOptions& options) {
    Operation op(recording_.get(), "threshold");
    const auto& target = op.resource(mask.resource_, "mask", ResourceKind::mask);
    op.require(std::isfinite(options.value) && options.value >= 0 && options.value <= 1, "value",
               "value must be finite and in [0, 1]");
    std::array<std::uint8_t, 256> table{};
    for (std::size_t i = 0; i < table.size(); ++i) {
        table[i] = double(i) / 255 >= options.value ? 255 : 0;
    }
    record_lut(op, target, table,
               options.mask ? op.resource(options.mask->resource_, "mask", ResourceKind::mask)
                            : nullptr,
               options.region);
}

void Commands::levels(const Mask& mask, const MaskLevelsOptions& options) {
    Operation op(recording_.get(), "levels");
    const auto& target = op.resource(mask.resource_, "mask", ResourceKind::mask);
    const auto unit = [&](float value, std::string_view parameter) {
        op.require(std::isfinite(value) && value >= 0 && value <= 1, parameter,
                   "value must be finite and in [0, 1]");
    };
    unit(options.transfer.input_black, "transfer.input_black");
    unit(options.transfer.input_white, "transfer.input_white");
    op.require(options.transfer.input_black < options.transfer.input_white, "transfer.input_white",
               "input white must exceed input black");
    op.require(std::isfinite(options.transfer.gamma) && options.transfer.gamma >= 0.01f &&
                   options.transfer.gamma <= 9.99f,
               "transfer.gamma", "gamma must be finite and in [0.01, 9.99]");
    unit(options.transfer.output_black, "transfer.output_black");
    unit(options.transfer.output_white, "transfer.output_white");
    std::array<std::uint8_t, 256> table{};
    for (std::size_t i = 0; i < table.size(); ++i) {
        const double input = (double(i) / 255 - options.transfer.input_black) /
                             (double(options.transfer.input_white) - options.transfer.input_black);
        const double midtones = std::pow(std::clamp(input, 0.0, 1.0), 1.0 / options.transfer.gamma);
        const double output =
            options.transfer.output_black +
            midtones * (double(options.transfer.output_white) - options.transfer.output_black);
        table[i] = static_cast<std::uint8_t>(std::floor(std::clamp(output, 0.0, 1.0) * 255 + 0.5));
    }
    record_lut(op, target, table,
               options.mask ? op.resource(options.mask->resource_, "mask", ResourceKind::mask)
                            : nullptr,
               options.region);
}
} // namespace wgpupixel
