#pragma once
#include "../utils/operations.h"
#include <functional>
#include <vector>

namespace wgpupixel::detail {
inline void require_geometry_filter(const Operation& op, ResizeFilter filter) {
    op.require(std::to_underlying(filter) <= std::to_underlying(ResizeFilter::area), "filter",
               "resize filter is invalid");
}
inline void require_edge(const Operation& op, EdgeMode edge) {
    op.require(std::to_underlying(edge) <= std::to_underlying(EdgeMode::mirror), "edge",
               "edge mode is invalid");
}

// Destination-to-source matrix rows for footprint_map (footprint.wgsl), scaled so the
// largest entry is one. The caller guarantees w > 0 inside the destination quad.
void set_projection(Record& record, const Homography& destination_to_source);

// Inverse of an affine transform in double precision; empty when singular or not finite.
std::optional<Homography> invert_affine(const Affine& matrix);

// Exact integer placement source = sign * destination + shift along one axis, with
// shift reduced to a small range that preserves the result for the edge mode.
std::int32_t reduce_shift(std::int64_t shift, int sign, std::uint32_t source_extent,
                          std::uint32_t destination_extent, EdgeMode edge);

// Float scratch image of width x height pixels, owned by the records that use it
// and released with them; allocated through allocate_buffer.
std::shared_ptr<Resource> scratch_image(const Resource& like, std::uint64_t width,
                                        std::uint64_t height, std::string_view operation);

// Pixel dimensions, so sampling decisions depend on sizes alone and requirements
// queries can repeat them without resources.
struct Extent {
    std::uint32_t width = 0, height = 0;
};
inline Extent extent(const Resource& resource) noexcept {
    return {resource.width, resource.height};
}
// Supplies a float temporary image of width x height pixels for internal passes.
using Temporary = std::function<std::shared_ptr<Resource>(std::uint32_t, std::uint32_t)>;

// Box pyramid levels footprint_map needs so that no footprint exceeds its direct
// sampling limit (0 when none). destination_to_source must have w > 0 on the
// destination except beyond a horizon.
std::uint32_t pyramid_levels(const Homography& destination_to_source, Extent source,
                             Extent destination, ResizeFilter filter, EdgeMode edge);

// Size to which an affine projection's source is first reduced (area) so that every
// footprint fits the direct sampling limit; the source size when none is needed.
Extent reduced_size(const Homography& destination_to_source, Extent source, ResizeFilter filter,
                    EdgeMode edge);

// Appends internal records averaging source (an image or a mask) exactly over each
// output pixel (ResizeFilter::area) into a new width x height float image.
std::shared_ptr<Resource> reduce_area(std::vector<Record>& records,
                                      const std::shared_ptr<Resource>& source, Extent size,
                                      const Temporary& temporary);

// Affine area reductions whose footprint exceeds the direct limit are integrated
// exactly per row with an area table (footprint.wgsl, area_integral); otherwise the
// source is first averaged (reduced_size). With transparent and clamp edges only
// pixels near the source rows do that work. Tiled edges (repeat, mirror) are exact
// while the pixels actually computed (work_pixels: the region, not the canvas) fit a
// fixed work budget.
inline constexpr std::uint32_t area_table_levels = 0xffffffffu;
bool use_area_table(const Homography& destination_to_source, std::uint64_t work_pixels,
                    EdgeMode edge, ResizeFilter filter);

// Appends the records building the area table of source (an image or a mask).
std::shared_ptr<Resource> build_area_table(std::vector<Record>& records,
                                           const std::shared_ptr<Resource>& source,
                                           const Temporary& temporary);

// Appends the records building `levels` pyramid levels of source (an image or a
// mask) and returns the pyramid.
std::shared_ptr<Resource> build_pyramid(std::vector<Record>& records,
                                        const std::shared_ptr<Resource>& source,
                                        std::uint32_t levels, EdgeMode edge,
                                        const Temporary& temporary);

// Resizes whose direct footprint exceeds this many taps run as one pass per axis
// through an intermediate reduced along the more reduced axis first.
struct ResizeSplit {
    bool split = false;
    std::uint32_t width = 0, height = 0; // Intermediate dimensions.
};
ResizeSplit resize_split(Extent source, Extent destination, ResizeFilter filter);

// How a projection samples its source, decided from sizes alone: directly, through
// an area table, after an area reduction (projection rescaled to the reduced
// source), or through a box pyramid. temporaries lists the float images it needs
// in the order they are created. work is the destination rectangle computed
// (left, top, right, bottom).
struct ProjectionPlan {
    enum class Method { direct, area_table, reduce, pyramid } method = Method::direct;
    Homography projection{};
    std::uint32_t levels = 0;
    Extent reduced{};
    std::vector<Extent> temporaries;
};
// Validates the source and destination sizes of a geometry requirements query.
void check_geometry_sizes(ImageSize source, ImageSize destination, std::string_view operation);

// Workspace plan holding float temporaries of the given sizes.
WorkspacePlan temporaries_plan(const std::vector<Extent>& temporaries, std::string_view operation);

ProjectionPlan plan_projection(const Homography& destination_to_source, Extent source,
                               Extent destination, ResizeFilter filter, EdgeMode edge,
                               std::array<std::int32_t, 4> work, std::string_view operation);

// Appends all records or none.
void append_records(Operation& op, const std::vector<Record>& records);

enum class MaskMapping : std::uint32_t {
    projection = 0,
    nearest_resize = 1,
    integer = 2,
    float_projection = 3,
    zoom = 4
};

// Validates a mask-to-mask geometry operation (selection mask, region, source and
// destination, in that order) and builds its records: one invocation per packed word.
class MaskGeometry {
  public:
    MaskGeometry(Operation& op, const std::shared_ptr<Resource>& source,
                 const std::shared_ptr<Resource>& destination,
                 const std::shared_ptr<Resource>& selection, bool selected,
                 const std::optional<Rect>& region);
    // source overrides the validated source (a float intermediate for float_projection).
    Record record(MaskMapping mapping, EdgeMode edge = EdgeMode::transparent,
                  ResizeFilter filter = ResizeFilter::nearest,
                  const std::shared_ptr<Resource>& source = {}) const;
    // Binds the context's placeholder where no pyramid is needed, then appends.
    void append(std::vector<Record> records) const;
    const std::shared_ptr<Resource>& source_resource() const noexcept {
        return source_;
    }
    const Resource& source() const noexcept {
        return *source_;
    }
    const Resource& destination() const noexcept {
        return *destination_;
    }

  private:
    Operation& op_;
    std::shared_ptr<Resource> source_, destination_;
    std::array<std::int32_t, 4> region_{};
};

// Sets the integer placement of MaskMapping::integer records.
void set_integer_mapping(Record& record, std::array<std::int32_t, 2> sign,
                         std::array<std::int32_t, 2> shift);
} // namespace wgpupixel::detail
