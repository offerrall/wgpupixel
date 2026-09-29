#include "utils/operations.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <vector>

namespace wgpupixel {
using namespace detail;
namespace {
constexpr std::uint32_t max_bins = 4096;
// Histogram workgroups keep 4096 local bins; statistics slots hold 8 channels x 5 moments.
constexpr std::uint32_t histogram_lanes = 256, statistics_lanes = 64;
constexpr std::uint64_t slot_words = 40, max_slots = 2048;
// Match mask_bounds.wgsl: 256 lanes, 16 texels per lane, at most 256 groups.
constexpr std::uint64_t bounds_group_pixels = 4096, bounds_dispatch_pixels = 1u << 20;
constexpr std::uint64_t bounds_bytes = 16;

std::shared_ptr<Resource> create_result(const std::shared_ptr<State>& state, ResourceKind kind,
                                        std::uint64_t capacity, std::uint64_t bytes,
                                        std::string_view operation) {
    auto resource = std::make_shared<Resource>();
    resource->owner = state;
    resource->kind = kind;
    resource->capacity = capacity;
    resource->buffer = allocate_buffer(
        *state, bytes, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst,
        operation);
    resource->readback = allocate_buffer(
        *state, bytes, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst, operation);
    state->register_resource(resource);
    return resource;
}

void require_readable(const Resource& resource, std::string_view operation) {
    if (resource.recorded || resource.pending) {
        fail(ErrorCode::resource_busy, operation, "buffer",
             "buffer is recorded or pending on the GPU");
    }
}

void destroy_result(const std::shared_ptr<State>& state, const std::shared_ptr<Resource>& resource,
                    ResourceKind kind) {
    constexpr std::string_view operation = "destroy";
    validate_resource(state, resource, kind, operation, "resource");
    if (resource->recorded || resource->pending) {
        fail(ErrorCode::resource_busy, operation, "resource",
             "resource is recorded or pending on the GPU");
    }
    resource->release();
    resource->alive = false;
    resource->valid_bytes = 0;
    std::erase_if(state->resources, [&](const auto& entry) {
        return entry.expired() || entry.lock() == resource;
    });
}

std::array<std::uint32_t, 4> measurement_area(const Operation& op, const Resource& source,
                                             const std::optional<Rect>& area) {
    std::int64_t left = 0, top = 0, right = source.width, bottom = source.height;
    if (area) {
        const auto& region = *area;
        op.require(region.width >= 0 && region.height >= 0, "region",
                   "region dimensions must be nonnegative");
        left = std::clamp<std::int64_t>(region.x, 0, right);
        top = std::clamp<std::int64_t>(region.y, 0, bottom);
        right = std::clamp<std::int64_t>(std::int64_t(region.x) + region.width, 0, right);
        bottom = std::clamp<std::int64_t>(std::int64_t(region.y) + region.height, 0, bottom);
        if (left >= right || top >= bottom) {
            left = right = top = bottom = 0;
        }
    }
    return {static_cast<std::uint32_t>(left), static_cast<std::uint32_t>(top),
            static_cast<std::uint32_t>(right - left), static_cast<std::uint32_t>(bottom - top)};
}

// Records a measurement of the region (clipped to the image) as a grid-stride dispatch of
// at most max_groups workgroups, each covering at least group_pixels pixels.
Record measurement(const Operation& op, Kernel kernel, const std::shared_ptr<Resource>& source,
                   const std::shared_ptr<Resource>& destination, const AnalysisOptions& options,
                   std::uint64_t group_pixels, std::uint64_t max_groups) {
    op.require(std::to_underlying(options.space) <= std::to_underlying(ColorEncoding::srgb),
               "space", "analysis space is invalid");
    const auto area = measurement_area(op, *source, options.region);
    const auto pixels = std::uint64_t(area[2]) * area[3];
    const auto groups =
        std::clamp<std::uint64_t>((pixels + group_pixels - 1) / group_pixels, 1, max_groups);
    auto record = kernel_record(kernel, source, destination);
    record.parameters.dimensions = {source->width, source->height, source->width, source->height};
    record.parameters.dispatch = area;
    record.parameters.reserved = {0, options.space == ColorEncoding::linear ? 1u : 0u,
                                  static_cast<std::uint32_t>(groups), 0};
    record.workgroups = {static_cast<std::uint32_t>(groups), 1, 1};
    return record;
}

// Undoes the shader's m2 scale: the largest power of two not above max(|min|, |max|, 1),
// at most 2^126.
double moment_scale(float minimum, float maximum) {
    const float magnitude = std::max({std::abs(minimum), std::abs(maximum), 1.0f});
    return std::ldexp(1.0,
                      int(std::min(std::bit_cast<std::uint32_t>(magnitude) >> 23, 253u)) - 127);
}

void merge(ChannelStatistics& total, double& count, const float* moments) {
    const double n = moments[0];
    if (n == 0) {
        return;
    }
    const double scale = moment_scale(moments[3], moments[4]);
    const double sum = count + n, delta = double(moments[1]) - total.mean;
    const double m2 =
        total.deviation + double(moments[2]) * scale * scale + delta * delta * count * n / sum;
    total.minimum = count ? std::min<double>(total.minimum, moments[3]) : moments[3];
    total.maximum = count ? std::max<double>(total.maximum, moments[4]) : moments[4];
    total.mean += delta * n / sum;
    total.deviation = m2; // Holds the sum of squared deviations until finished.
    count = sum;
}
} // namespace

Rect sample_region(Point point, std::int32_t size) {
    constexpr std::string_view operation = "sample_region";
    if (size < 1 || size % 2 == 0) {
        fail(ErrorCode::invalid_argument, operation, "size", "size must be odd and positive");
    }
    constexpr double low = std::numeric_limits<std::int32_t>::min();
    constexpr double high = std::numeric_limits<std::int32_t>::max();
    const double x = std::floor(double(point.x)) - size / 2;
    const double y = std::floor(double(point.y)) - size / 2;
    if (!(x >= low && y >= low && x <= high && y <= high)) {
        fail(ErrorCode::invalid_argument, operation, "point",
             "point must be finite and inside the 32-bit coordinate range");
    }
    return {static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), size, size};
}

HistogramBuffer::HistogramBuffer(std::shared_ptr<Resource> resource)
    : resource_(std::move(resource)) {}
StatisticsBuffer::StatisticsBuffer(std::shared_ptr<Resource> resource)
    : resource_(std::move(resource)) {}
MaskBoundsBuffer::MaskBoundsBuffer(std::shared_ptr<Resource> resource)
    : resource_(std::move(resource)) {}

std::uint32_t HistogramBuffer::bins() const {
    constexpr std::string_view operation = "bins";
    auto state = resource_owner(resource_, operation);
    std::lock_guard lock(state->mutex);
    state->check(operation);
    validate_resource(state, resource_, ResourceKind::histogram, operation, "buffer");
    return static_cast<std::uint32_t>(resource_->capacity);
}

HistogramBuffer Context::create_histogram_buffer(std::uint32_t bins) try {
    constexpr std::string_view operation = "create_histogram_buffer";
    auto state = require_state(state_, operation);
    std::lock_guard lock(state->mutex);
    state->check(operation);
    if (bins < 2 || bins > max_bins) {
        fail(ErrorCode::invalid_argument, operation, "bins", "bins must lie in [2, 4096]");
    }
    return HistogramBuffer(create_result(state, ResourceKind::histogram, bins,
                                         std::uint64_t(bins) * analysis_channel_count * 4,
                                         operation));
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, "create_histogram_buffer", "",
         "could not allocate histogram resources");
}

StatisticsBuffer Context::create_statistics_buffer() try {
    constexpr std::string_view operation = "create_statistics_buffer";
    auto state = require_state(state_, operation);
    std::lock_guard lock(state->mutex);
    state->check(operation);
    return StatisticsBuffer(create_result(state, ResourceKind::statistics, max_slots,
                                          max_slots * slot_words * 4, operation));
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, "create_statistics_buffer", "",
         "could not allocate statistics resources");
}

MaskBoundsBuffer Context::create_mask_bounds_buffer() try {
    constexpr std::string_view operation = "create_mask_bounds_buffer";
    auto state = require_state(state_, operation);
    std::lock_guard lock(state->mutex);
    state->check(operation);
    return MaskBoundsBuffer(create_result(state, ResourceKind::mask_bounds, 1,
                                          bounds_bytes, operation));
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, "create_mask_bounds_buffer", "",
         "could not allocate mask bounds resources");
}

std::optional<Rect> Context::read(const MaskBoundsBuffer& buffer) try {
    constexpr std::string_view operation = "read";
    auto state = require_state(state_, operation);
    std::lock_guard lock(state->mutex);
    state->check(operation);
    validate_resource(state, buffer.resource_, ResourceKind::mask_bounds, operation, "buffer");
    const auto& resource = *buffer.resource_;
    require_readable(resource, operation);
    if (resource.valid_bytes != bounds_bytes) {
        fail(ErrorCode::invalid_argument, operation, "buffer",
             "buffer holds no completed mask bounds");
    }
    std::array<std::uint32_t, 4> bounds{};
    read_mapped(*state, resource, std::as_writable_bytes(std::span(bounds)), operation);
    if (bounds[2] == 0) return std::nullopt;
    // Complemented minima let every component reduce with max from a zero clear.
    const auto x = ~bounds[0], y = ~bounds[1];
    return Rect{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y),
                static_cast<std::int32_t>(bounds[2] - x),
                static_cast<std::int32_t>(bounds[3] - y)};
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, "read", "", "could not prepare readback mapping");
}

void Context::read(const HistogramBuffer& buffer, std::span<std::uint32_t> counts) try {
    constexpr std::string_view operation = "read";
    auto state = require_state(state_, operation);
    std::lock_guard lock(state->mutex);
    state->check(operation);
    validate_resource(state, buffer.resource_, ResourceKind::histogram, operation, "buffer");
    const auto& resource = *buffer.resource_;
    require_readable(resource, operation);
    if (counts.size() != resource.capacity * analysis_channel_count) {
        fail(ErrorCode::invalid_argument, operation, "counts",
             "counts must hold analysis_channel_count * bins values");
    }
    if (resource.valid_bytes != counts.size_bytes()) {
        fail(ErrorCode::invalid_argument, operation, "buffer",
             "buffer holds no completed histogram");
    }
    read_mapped(*state, resource, std::as_writable_bytes(counts), operation);
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, "read", "", "could not prepare readback mapping");
}

ImageStatistics Context::read(const StatisticsBuffer& buffer) try {
    constexpr std::string_view operation = "read";
    auto state = require_state(state_, operation);
    std::lock_guard lock(state->mutex);
    state->check(operation);
    validate_resource(state, buffer.resource_, ResourceKind::statistics, operation, "buffer");
    const auto& resource = *buffer.resource_;
    require_readable(resource, operation);
    if (resource.valid_bytes == 0) {
        fail(ErrorCode::invalid_argument, operation, "buffer",
             "buffer holds no completed statistics");
    }
    std::vector<float> partials(resource.valid_bytes / 4);
    read_mapped(*state, resource, std::as_writable_bytes(std::span(partials)), operation);
    // Channel order of each slot: red, green, blue, alpha, luminosity, premultiplied RGB.
    std::array<ChannelStatistics, 8> channels{};
    std::array<double, 8> counts{};
    for (std::size_t slot = 0; slot + slot_words <= partials.size(); slot += slot_words) {
        for (std::size_t c = 0; c < channels.size(); ++c) {
            merge(channels[c], counts[c], partials.data() + slot + c * 5);
        }
    }
    for (std::size_t c = 0; c < channels.size(); ++c) {
        channels[c].deviation = counts[c] ? std::sqrt(channels[c].deviation / counts[c]) : 0;
    }
    ImageStatistics result;
    result.pixels = static_cast<std::uint64_t>(counts[3]);
    result.color_pixels = static_cast<std::uint64_t>(counts[0]);
    result.red = channels[0];
    result.green = channels[1];
    result.blue = channels[2];
    result.alpha = channels[3];
    result.luminosity = channels[4];
    result.average = {float(channels[5].mean), float(channels[6].mean), float(channels[7].mean),
                      float(channels[3].mean)};
    return result;
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, "read", "", "could not prepare readback mapping");
}

void Context::destroy(const HistogramBuffer& buffer) {
    auto state = require_state(state_, "destroy");
    std::lock_guard lock(state->mutex);
    destroy_result(state, buffer.resource_, ResourceKind::histogram);
}

void Context::destroy(const StatisticsBuffer& buffer) {
    auto state = require_state(state_, "destroy");
    std::lock_guard lock(state->mutex);
    destroy_result(state, buffer.resource_, ResourceKind::statistics);
}

void Context::destroy(const MaskBoundsBuffer& buffer) {
    auto state = require_state(state_, "destroy");
    std::lock_guard lock(state->mutex);
    destroy_result(state, buffer.resource_, ResourceKind::mask_bounds);
}

void Commands::mask_bounds(const Mask& source, const MaskBoundsBuffer& destination,
                           const MaskBoundsOptions& options) {
    Operation op(recording_.get(), "mask_bounds");
    const auto& src = op.resource(source.resource_, "source", ResourceKind::mask);
    const auto& dst = op.resource(destination.resource_, "destination", ResourceKind::mask_bounds);
    op.require(std::isfinite(options.threshold) && options.threshold >= 0 && options.threshold <= 1,
               "threshold", "threshold must be finite and in [0, 1]");
    // Multiply in float so normalized A8 values (including the default) round back
    // to their byte before ceil; promoting 1.0f / 255.0f to double would yield 2.
    const auto threshold = static_cast<std::uint32_t>(std::ceil(options.threshold * 255.0f));
    const auto area = measurement_area(op, *src, options.region);
    const auto pixels = std::uint64_t(area[2]) * area[3];
    const auto chunks = std::max<std::uint64_t>(1, (pixels + bounds_dispatch_pixels - 1) /
                                                     bounds_dispatch_pixels);
    op.reserve(chunks);
    for (std::uint64_t chunk = 0; chunk < chunks; ++chunk) {
        const auto first = chunk * bounds_dispatch_pixels;
        const auto count = std::min(bounds_dispatch_pixels, pixels - first);
        auto record = kernel_record(Kernel::mask_bounds, src, dst);
        record.parameters.dispatch = area;
        record.parameters.reserved = {threshold, static_cast<std::uint32_t>(first),
                                      static_cast<std::uint32_t>(count), 0};
        record.workgroups = {static_cast<std::uint32_t>(std::max<std::uint64_t>(
                                 1, (count + bounds_group_pixels - 1) / bounds_group_pixels)), 1, 1};
        record.bytes = bounds_bytes;
        record.accumulate_result = chunk != 0;
        record.defer_readback = chunk + 1 != chunks;
        record.starts_batch = chunk % 8 == 0;
        op.append({record});
    }
}

void Commands::histogram(const Image& source, const HistogramBuffer& destination,
                         const AnalysisOptions& options) {
    Operation op(recording_.get(), "histogram");
    op.coverage(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                source.resource_, options.mask != nullptr, "source");
    const auto& src = op.resource(source.resource_, "source");
    const auto& dst = op.resource(destination.resource_, "destination", ResourceKind::histogram);
    const auto bins = static_cast<std::uint32_t>(dst->capacity);
    // Channels sharing one workgroup's local bins; more bins need more layers.
    const auto per_layer = std::min(analysis_channel_count, max_bins / bins);
    auto record = measurement(op, Kernel::histogram, src, dst, options,
                              std::uint64_t(histogram_lanes) * 64, 1024);
    record.parameters.reserved[0] = bins;
    record.parameters.reserved[3] = per_layer;
    record.workgroups[2] = (analysis_channel_count + per_layer - 1) / per_layer;
    record.bytes = std::uint64_t(bins) * analysis_channel_count * 4;
    op.append({record});
}

void Commands::statistics(const Image& source, const StatisticsBuffer& destination,
                          const AnalysisOptions& options) {
    Operation op(recording_.get(), "statistics");
    op.coverage(options.mask ? options.mask->resource_ : std::shared_ptr<Resource>{},
                source.resource_, options.mask != nullptr, "source");
    const auto& src = op.resource(source.resource_, "source");
    const auto& dst = op.resource(destination.resource_, "destination", ResourceKind::statistics);
    auto record = measurement(op, Kernel::statistics, src, dst,
                              {.space = options.space, .region = options.region},
                              std::uint64_t(statistics_lanes) * 64, max_slots);
    record.bytes = std::uint64_t(record.workgroups[0]) * slot_words * 4;
    op.append({record});
}
} // namespace wgpupixel
