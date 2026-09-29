#include "commands.h"
#include <algorithm>
#include <new>
#include <limits>
#include <utility>

namespace wgpupixel::detail {

Recording::~Recording() {
    if (owner && !records.empty()) {
        std::lock_guard lock(owner->mutex);
        clear();
    }
}

void Recording::clear() noexcept {
    rollback(0, 0, 0);
}

void Recording::rollback(std::size_t record_count, std::size_t data_words,
                         std::uint32_t dissolve_index) noexcept {
    for (std::size_t i = record_count; i < records.size(); ++i) {
        const auto& record = records[i];
        if (record.recording_guard) record.recording_guard->invalidate();
        for (const auto& source : record.sources) {
            if (source) --source->recorded;
        }
        if (record.source) {
            --record.source->recorded;
        }
        if (record.destination) {
            --record.destination->recorded;
        }
        if (record.coverage) {
            --record.coverage->recorded;
        }
    }
    records.resize(record_count);
    data.resize(data_words);
    next_dissolve_index = dissolve_index;
}

void Recording::require_ready(std::string_view operation) const {
    if (flight && !flight->retired) {
        fail(ErrorCode::resource_busy, operation, "commands",
             "wait for the submission before reusing commands");
    }
}

void Recording::recycle() noexcept {
    if (flight) {
        // require_ready must succeed first; retirement has cleared these records.
        records.swap(flight->records);
        flight.reset();
        next_dissolve_index = 0;
    }
}

void Recording::require_slots(std::size_t count, std::string_view operation) try {
    const auto maximum = std::min({std::uint64_t(records.max_size()),
                                    std::uint64_t(parameters.max_size() / stride),
                                    owner->limits.maxBufferSize / stride});
    if (records.size() > maximum || count > maximum - records.size()) {
        fail(ErrorCode::capacity, operation, "commands", "commands exceed device or host limits");
    }
    const auto needed = records.size() + count;
    if (needed <= capacity) return;
    const auto grown = std::max<std::uint64_t>(needed, std::min(maximum, 2 * std::uint64_t(capacity)));
    records.reserve(grown);
    parameters.resize(grown * stride);
    auto buffer = allocate_buffer(*owner, grown * stride,
                                  WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, operation);
    uniform = std::move(buffer);
    capacity = grown;
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, operation, "commands", "host allocation failed");
}

void Recording::append(Record record) noexcept {
    records.push_back(std::move(record));
    const auto& stored = records.back();
    for (const auto& source : stored.sources) {
        if (source) ++source->recorded;
    }
    if (stored.source) {
        ++stored.source->recorded;
    }
    if (stored.destination) {
        ++stored.destination->recorded;
    }
    if (stored.coverage) {
        ++stored.coverage->recorded;
    }
}

namespace {

Recording& require_recording(Recording* value, std::string_view name) {
    if (!value || !value->owner) {
        fail(ErrorCode::invalid_resource, name, "commands", "commands are not initialized");
    }
    return *value;
}

Parameters dimensions(const Resource& source, const Resource& destination) {
    Parameters result;
    result.dimensions = {source.width, source.height, destination.width, destination.height};
    result.dispatch = {0, 0, destination.width, destination.height};
    return result;
}

} // namespace

Operation::Operation(Recording* value, std::string_view name)
    : recording_(require_recording(value, name)), lock_(recording_.owner->mutex), name_(name) {
    recording_.owner->check(name_);
    recording_.require_ready(name_);
    recording_.recycle();
    record_mark_ = recording_.records.size();
    capacity_mark_ = recording_.capacity;
    dissolve_mark_ = recording_.next_dissolve_index;
    uniform_mark_ = recording_.uniform.retain();
    data_mark_ = data_end_ = recording_.data.size();
}

Operation::~Operation() {
    if (std::uncaught_exceptions() > exceptions_) {
        recording_.rollback(record_mark_, data_mark_, dissolve_mark_);
        recording_.uniform = std::move(uniform_mark_);
        recording_.capacity = capacity_mark_;
        recording_.parameters.resize(capacity_mark_ * recording_.stride);
    } else {
        // Drop words staged for records this operation did not append.
        recording_.data.resize(std::max(data_mark_, data_end_));
    }
}

std::span<std::uint32_t> Operation::data(Record& record, std::size_t count,
                                         std::string_view parameter) {
    const auto& limits = recording_.owner->limits;
    require(kernel_data(record.kernel) && record.data_offset == no_data, parameter,
            "kernel does not accept staged data", ErrorCode::execution_failed);
    const auto words = std::max<std::uint64_t>(count, 1);
    const auto alignment = std::max<std::uint64_t>(limits.minStorageBufferOffsetAlignment / 4, 1);
    const auto offset = (recording_.data.size() + alignment - 1) / alignment * alignment;
    require(count <= limits.maxStorageBufferBindingSize / 4, parameter,
            "data exceeds the device storage binding size", ErrorCode::capacity);
    require(offset + words <= limits.maxBufferSize / 4 &&
                offset + words <= recording_.data.max_size(),
            parameter, "recorded data exceeds device buffer capacity", ErrorCode::capacity);
    try {
        recording_.data.resize(offset + words);
    } catch (const std::bad_alloc&) {
        fail(ErrorCode::out_of_memory, name_, parameter, "host allocation failed");
    }
    record.data_offset = offset;
    record.data_count = count;
    return std::span(recording_.data).subspan(offset, count);
}

const std::shared_ptr<Resource>& Operation::resource(const std::shared_ptr<Resource>& value,
                                                     std::string_view parameter,
                                                     ResourceKind kind) const {
    validate_resource(recording_.owner, value, kind, name_, parameter);
    return value;
}

void Operation::require(bool condition, std::string_view parameter, std::string_view message,
                        ErrorCode code) const {
    if (!condition) {
        fail(code, name_, parameter, message);
    }
}

void Operation::distinct(const Resource& source, const Resource& destination,
                         std::string_view parameter) const {
    require(source.buffer != destination.buffer, parameter,
            "source and destination must not overlap");
}

void Operation::same_size(const Resource& source, const Resource& destination,
                          std::string_view parameter) const {
    require(source.width == destination.width && source.height == destination.height, parameter,
            "images must have equal logical dimensions");
}

void Operation::coverage(const std::shared_ptr<Resource>& mask,
                         const std::shared_ptr<Resource>& target, bool supplied,
                         std::string_view target_parameter) {
    if (!supplied) {
        return;
    }
    resource(mask, "mask", ResourceKind::mask);
    resource(target, target_parameter,
             target && target->kind == ResourceKind::mask ? ResourceKind::mask : ResourceKind::image);
    same_size(*mask, *target, "mask");
    coverage_ = mask;
}

void Operation::region(const Rect* value) {
    if (value) {
        require(value->width >= 0 && value->height >= 0, "region",
                "region dimensions must be nonnegative");
    }
    region_ = value;
}

void Operation::reserve(std::size_t count) const {
    recording_.require_slots(count, name_);
    recording_.recycle();
}

void Operation::append(std::initializer_list<Record> records) const {
    for (const auto& record : records) {
        require(!kernel_data(record.kernel) || record.data_offset != no_data, "data",
                "kernel data was not staged", ErrorCode::execution_failed);
    }
    // Check the whole operation before recording either pass of a blur.
    recording_.require_slots(records.size(), name_);
    recording_.recycle();
    for (auto record : records) {
        // Intermediate passes write scratch images; mask and region apply to the output.
        const bool internal =
            record.kernel == Kernel::resize_pass || record.kernel == Kernel::geometry_pyramid ||
            record.kernel == Kernel::area_table;
        // Blur reads the whole neighborhood; coverage applies only to its final write.
        if (!record.unmasked && record.kernel != Kernel::blur_horizontal && !internal) {
            record.coverage = coverage_;
        }
        if (region_ && !internal) {
            const auto& dimensions = record.parameters.dimensions;
            const auto width = std::int64_t(dimensions[2]);
            const auto height = std::int64_t(dimensions[3]);
            auto left = std::clamp<std::int64_t>(region_->x, 0, width);
            auto top = std::clamp<std::int64_t>(region_->y, 0, height);
            auto right = std::clamp<std::int64_t>(std::int64_t(region_->x) + region_->width, 0, width);
            auto bottom = std::clamp<std::int64_t>(std::int64_t(region_->y) + region_->height, 0, height);
            if (left >= right || top >= bottom) { continue; }
            if (record.kernel == Kernel::blur_horizontal) {
                const auto radius = std::int64_t(record.parameters.offsets[2]);
                top = std::max<std::int64_t>(0, top - radius);
                bottom = std::min(height, bottom + radius);
            }
            if (record.kernel == Kernel::blend || record.kernel == Kernel::mask) {
                const auto x = std::int64_t(record.parameters.offsets[0]);
                const auto y = std::int64_t(record.parameters.offsets[1]);
                left = std::max<std::int64_t>(0, left - x);
                top = std::max<std::int64_t>(0, top - y);
                right = std::min<std::int64_t>(dimensions[0], right - x);
                bottom = std::min<std::int64_t>(dimensions[1], bottom - y);
                if (left >= right || top >= bottom) { continue; }
            }
            const auto& dispatch = record.parameters.dispatch;
            left = std::max<std::int64_t>(left, dispatch[0]);
            top = std::max<std::int64_t>(top, dispatch[1]);
            right = std::min<std::int64_t>(right, std::int64_t(dispatch[0]) + dispatch[2]);
            bottom = std::min<std::int64_t>(bottom, std::int64_t(dispatch[1]) + dispatch[3]);
            if (left >= right || top >= bottom) { continue; }
            record.parameters.dispatch = {static_cast<std::uint32_t>(left),
                                          static_cast<std::uint32_t>(top),
                                          static_cast<std::uint32_t>(right - left),
                                          static_cast<std::uint32_t>(bottom - top)};
        }
        if (record.data_offset != no_data) {
            data_end_ = std::max<std::size_t>(
                data_end_, record.data_offset + std::max<std::uint64_t>(record.data_count, 1));
        }
        recording_.append(std::move(record));
    }
}

Record kernel_record(Kernel kernel, const std::shared_ptr<Resource>& source,
                     const std::shared_ptr<Resource>& destination) {
    Record record;
    record.kernel = kernel;
    record.source = source;
    record.destination = destination;
    record.parameters = dimensions(*source, destination ? *destination : *source);
    if (kernel == Kernel::blend || kernel == Kernel::mask) {
        record.parameters.dispatch = {0, 0, source->width, source->height};
    }
    return record;
}

Record copy_record(const std::shared_ptr<Resource>& source,
                   const std::shared_ptr<Resource>& destination) {
    const auto& image = (source->kind == ResourceKind::image || source->kind == ResourceKind::mask)
                            ? *source
                            : *destination;
    Record record;
    record.kind = RecordKind::copy;
    record.source = source;
    record.destination = destination;
    record.parameters = dimensions(image, image);
    record.bytes = std::uint64_t(image.width) * image.height * pixel_bytes;
    return record;
}

} // namespace wgpupixel::detail
