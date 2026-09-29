#include "selection.h"
#include <limits>

namespace wgpupixel {
using namespace detail;
namespace {
constexpr std::int64_t sample_limit = 50;

bool valid_tolerance(float tolerance) {
    return std::isfinite(tolerance) && tolerance >= 0 && tolerance <= 1;
}

ImageSize wand_scratch(ImageSize source) {
    // Labels, then the reference color's four words.
    return {source.width, 4 * source.height + (16 + source.width - 1) / source.width};
}

// Straight linear to 0-255-scale sRGB, extended with odd symmetry beyond [0, 1] like
// straight_srgb255 in selection_common.wgsl.
double srgb255(double linear) {
    const double magnitude = std::abs(linear);
    const double encoded =
        magnitude <= 0.0031308 ? 12.92 * magnitude : 1.055 * std::pow(magnitude, 1 / 2.4) - 0.055;
    return std::clamp(std::copysign(encoded * 255, linear), -65535.0, 65535.0);
}
} // namespace

OperationRequirements select_magic_wand_requirements(ImageSize source,
                                                     const MagicWandOptions& options) {
    constexpr std::string_view operation = "select_magic_wand_requirements";
    constexpr auto maximum = std::numeric_limits<std::int32_t>::max();
    if (!valid_tolerance(options.tolerance)) {
        fail(ErrorCode::invalid_argument, operation, "tolerance",
             "tolerance must be finite and in [0, 1]");
    }
    if (options.sample_radius < 0 || options.sample_radius > sample_limit) {
        fail(ErrorCode::invalid_argument, operation, "sample_radius",
             "sample radius must be in [0, 50]");
    }
    if (source.width <= 0 || source.height <= 0 || source.width > maximum ||
        source.height > maximum) {
        fail(ErrorCode::invalid_argument, operation, "source",
             "source dimensions must be positive 32-bit signed integers");
    }
    if (source.width * source.height > std::int64_t{std::numeric_limits<std::uint32_t>::max()} ||
        wand_scratch(source).height > maximum) {
        fail(ErrorCode::capacity, operation, "source", "scratch exceeds shader address capacity");
    }
    return {source, scratch_plan(wand_scratch(source), operation)};
}

void Commands::select_color_range(const Image& source, const Mask& destination,
                                  const ColorRangeOptions& options) {
    Operation op(recording_.get(), "select_color_range");
    const auto& src = op.resource(source.resource_, "source");
    const auto& dst = op.resource(destination.resource_, "destination", ResourceKind::mask);
    op.same_size(*src, *dst);
    require_color(op, options.color, "color");
    op.require(options.color.a > 0, "color", "color alpha must be positive");
    op.require(std::isfinite(options.fuzziness) && options.fuzziness >= 0 && options.fuzziness <= 1,
               "fuzziness", "fuzziness must be finite and in [0, 1]");
    require_mode(op, options.mode);
    const double alpha = options.color.a;
    auto record = kernel_record(Kernel::color_range, src, dst);
    record.parameters.color1 = {float(srgb255(options.color.r / alpha)),
                                float(srgb255(options.color.g / alpha)),
                                float(srgb255(options.color.b / alpha)), 0};
    // Zero fuzziness still accepts colors within half an 8-bit step.
    record.parameters.values[0] = std::max(options.fuzziness * (255.0f * std::sqrt(3.0f)), 0.5f);
    record.parameters.reserved[0] = std::to_underlying(options.mode);
    record.parameters.dispatch = linear_dispatch(mask_words(*dst));
    op.append({record});
}

void Commands::select_magic_wand(const Image& source, const Mask& destination,
                                 const MagicWandOptions& options) {
    Operation op(recording_.get(), "select_magic_wand");
    const auto& src = op.resource(source.resource_, "source");
    const auto& dst = op.resource(destination.resource_, "destination", ResourceKind::mask);
    op.same_size(*src, *dst);
    op.require(valid_tolerance(options.tolerance), "tolerance",
               "tolerance must be finite and in [0, 1]");
    op.require(options.sample_radius >= 0 && options.sample_radius <= sample_limit, "sample_radius",
               "sample radius must be in [0, 50]");
    op.require(options.seed.x >= 0 && options.seed.y >= 0 &&
                   std::uint32_t(options.seed.x) < src->width &&
                   std::uint32_t(options.seed.y) < src->height,
               "seed", "seed must lie inside the image");
    require_mode(op, options.mode);
    const auto work = scratch_view(op, options.workspace.storage_, wand_scratch(size_of(*src)));
    const std::array<std::int32_t, 4> size{static_cast<std::int32_t>(src->width),
                                           static_cast<std::int32_t>(src->height), 0, 0};
    const std::uint32_t diagonal = options.diagonal ? 1 : 0;

    auto reference = kernel_record(Kernel::wand_reference, src, work);
    reference.parameters.offsets = {options.seed.x, options.seed.y, 0, 0};
    reference.parameters.reserved[0] = static_cast<std::uint32_t>(options.sample_radius);
    reference.parameters.dispatch = {0, 0, 8, 8};
    auto label = kernel_record(Kernel::wand_label, src, work);
    label.parameters.values[0] = options.tolerance * 255.0f;
    label.parameters.reserved[0] = diagonal;
    // 16x16 tiles, one workgroup each.
    label.parameters.dispatch = {0, 0, (src->width + 15) / 16 * 8, (src->height + 15) / 16 * 8};
    auto merge = kernel_record(Kernel::wand_merge, work);
    merge.parameters.offsets = size;
    merge.parameters.reserved[0] = diagonal;
    merge.parameters.dispatch = {0, 0, src->width, src->height};
    auto compress = kernel_record(Kernel::wand_compress, work);
    compress.parameters.offsets = size;
    compress.parameters.dispatch = merge.parameters.dispatch;
    auto select = kernel_record(Kernel::wand_select, work, dst);
    select.parameters.reserved = {std::to_underlying(options.mode), options.contiguous ? 1u : 0u,
                                  options.anti_alias ? 1u : 0u,
                                  std::uint32_t(options.seed.y) * src->width +
                                      std::uint32_t(options.seed.x)};
    select.parameters.dispatch = linear_dispatch(mask_words(*dst));
    if (options.contiguous) {
        op.append({reference, label, merge, compress, select});
    } else {
        op.append({reference, label, select});
    }
}
} // namespace wgpupixel
