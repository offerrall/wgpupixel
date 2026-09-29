#pragma once

#include "runtime.h"
#include <algorithm>
#include <bit>
#include <initializer_list>
#include <span>

namespace wgpupixel::detail {

// Holds the recording lock from validation through append.
class Operation {
  public:
    Operation(Recording*, std::string_view name);
    ~Operation();
    Operation(const Operation&) = delete;
    Operation& operator=(const Operation&) = delete;
    const std::shared_ptr<Resource>& resource(const std::shared_ptr<Resource>&,
                                              std::string_view parameter,
                                              ResourceKind = ResourceKind::image) const;
    std::string_view name() const noexcept { return name_; }
    void require(bool condition, std::string_view parameter, std::string_view message,
                 ErrorCode = ErrorCode::invalid_argument) const;
    void distinct(const Resource& source, const Resource& destination,
                  std::string_view parameter = "destination") const;
    void same_size(const Resource& source, const Resource& destination,
                   std::string_view parameter = "destination") const;
    void selection(const std::shared_ptr<Resource>& mask, const std::shared_ptr<Resource>& target,
                   bool supplied, const std::optional<Rect>& area) {
        coverage(mask, target, supplied);
        region(area ? &*area : nullptr);
    }
    void coverage(const std::shared_ptr<Resource>& mask, const std::shared_ptr<Resource>& target,
                  bool supplied, std::string_view target_parameter = "destination");
    void region(const Rect*);
    void append(std::initializer_list<Record>) const;
    void reserve(std::size_t count) const;

    // Validate the whole plan before appending and consume its slots per call.
    void workspace(const std::shared_ptr<WorkspaceStorage>&, const WorkspacePlan&);
    std::shared_ptr<Resource> temporary(std::uint32_t width, std::uint32_t height,
                                        ResourceKind kind = ResourceKind::image);

    // Variable-length kernel data. Stages `count` 32-bit words for `record`, whose
    // kernel must declare @binding(7) (see parameters.wgsl), and returns them to fill.
    // Submit uploads all staged words once into a reusable storage buffer; the kernel
    // binds exactly this slice. Limits: count * 4 must fit maxStorageBufferBindingSize
    // and the recording's aligned total must fit maxBufferSize (ErrorCode::capacity,
    // reported for `parameter`). Call once per record, before append. Words staged
    // by an operation that throws or skips its records are discarded; submit clears
    // the rest. Every data kernel record must stage data, even an empty payload.
    std::span<std::uint32_t> data(Record& record, std::size_t count, std::string_view parameter);
    template <class T>
        requires(sizeof(T) == 4 && std::is_trivially_copyable_v<T>)
    void data(Record& record, std::span<const T> values, std::string_view parameter) {
        std::ranges::transform(values, data(record, values.size(), parameter).begin(),
                               [](T value) { return std::bit_cast<std::uint32_t>(value); });
    }

  private:
    std::shared_ptr<WorkspaceStorage> workspace_;
    std::vector<bool> workspace_used_;
    std::shared_ptr<Resource> coverage_;
    const Rect* region_ = nullptr;
    Recording& recording_;
    std::unique_lock<std::mutex> lock_;
    std::string_view name_;
    int exceptions_ = std::uncaught_exceptions();
    std::size_t record_mark_ = 0, capacity_mark_ = 0;
    std::uint32_t dissolve_mark_ = 0;
    GpuBuffer uniform_mark_;
    std::size_t data_mark_ = 0;
    mutable std::size_t data_end_ = 0;
};

Record kernel_record(Kernel, const std::shared_ptr<Resource>& source,
                     const std::shared_ptr<Resource>& destination = {});
Record copy_record(const std::shared_ptr<Resource>& source,
                   const std::shared_ptr<Resource>& destination);

} // namespace wgpupixel::detail
