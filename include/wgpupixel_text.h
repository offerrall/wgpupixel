#pragma once
#include "wgpupixel.h"
#include <string>
#include <vector>

#if defined(_WIN32) && defined(WGPUPIXEL_TEXT_SHARED)
#if defined(WGPUPIXEL_BUILDING_TEXT)
#define WGPUPIXEL_TEXT_API __declspec(dllexport)
#else
#define WGPUPIXEL_TEXT_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#define WGPUPIXEL_TEXT_API __attribute__((visibility("default")))
#else
#define WGPUPIXEL_TEXT_API
#endif

namespace wgpupixel::text {
namespace detail {
struct Collection;
struct LayoutData;
} // namespace detail
enum class Wrap { word, character, word_char };
enum class Alignment { left, center, right };
enum class Direction { auto_detect, left_to_right, right_to_left };
enum class RasterFormat { a8, rgba8 };
struct Rect {
    double x = 0, y = 0, width = 0, height = 0;
};
struct TextStyle {
    std::string family;
    double size_px = 16;
    int weight = 400;
    bool italic = false;
    std::string features, variations, language;
    double letter_spacing_px = 0;
    // Linear premultiplied RGB; alpha in [0,1]. RGB clips to SDR only when rasterized.
    Color color{1, 1, 1, 1};
};
// Complete replacement styles; sorted, nonoverlapping UTF-8 byte ranges.
struct StyleRange {
    std::size_t start_byte = 0, end_byte = 0;
    TextStyle style;
};
struct ParagraphStyle {
    double width_px = -1;
    Wrap wrap = Wrap::word_char;
    Alignment alignment = Alignment::left;
    Direction direction = Direction::auto_detect;
    bool justify = false;
    double line_spacing_px = 0;
};
struct Metrics {
    Rect ink, logical;
    double baseline_px = 0;
    int line_count = 0, unknown_glyphs = 0;
};
// trailing is a count of Unicode characters in the grapheme, NOT bytes.
struct HitTest {
    std::size_t byte_index = 0;
    int trailing = 0;
    bool inside = false;
};
struct Caret {
    Rect strong, weak;
};
// Rows are tightly packed. Origin is relative to the layout top-left; empty ink has 0 dimensions.
// a8 is glyph coverage (ignores all style colors/alpha); rgba8 is straight-alpha sRGB.
struct Raster {
    std::uint32_t width = 0, height = 0, stride = 0;
    std::int32_t origin_x = 0, origin_y = 0;
    RasterFormat format = RasterFormat::rgba8;
    std::vector<std::uint8_t> pixels;
};
class WGPUPIXEL_TEXT_API Layout {
  public:
    Layout() noexcept = default;
    [[nodiscard]] Metrics metrics() const;
    [[nodiscard]] HitTest hit_test(double x, double y) const;
    [[nodiscard]] Caret caret(std::size_t byte_index) const;
    [[nodiscard]] std::vector<Rect> selection(std::size_t start_byte, std::size_t end_byte) const;
    [[nodiscard]] Raster rasterize(RasterFormat format = RasterFormat::rgba8) const;

  private:
    std::shared_ptr<detail::LayoutData> data_;
    explicit Layout(std::shared_ptr<detail::LayoutData>);
    friend class FontCollection;
};
// Private collection: no process-global registration or system-font fallback.
// Bytes and file contents are copied into private temporary storage, retained by layouts.
// Shared handles are safe for concurrent calls (internally serialized).
// Registration rejects duplicate family/face identities, and is resource_busy while
// layouts exist. Input limits: font 64 MiB, text 1 MiB, raster 256 MiB/32767px per axis.
// Paragraphs also have a conservative fixed-point coordinate complexity budget.
class WGPUPIXEL_TEXT_API FontCollection {
  public:
    FontCollection();
    [[nodiscard]] std::vector<std::string> add_file(std::string_view path);
    [[nodiscard]] std::vector<std::string> add_bytes(std::span<const std::uint8_t> bytes);
    [[nodiscard]] std::vector<std::string> families() const;
    [[nodiscard]] Layout layout(std::string_view utf8, const TextStyle& style,
                                const ParagraphStyle& paragraph = {},
                                std::span<const StyleRange> ranges = {}) const;

  private:
    std::shared_ptr<detail::Collection> data_;
};
} // namespace wgpupixel::text
