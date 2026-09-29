#include "wgpupixel_text.h"
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>
#include <fontconfig/fontconfig.h>
#include <hb.h>
#include <glib/gstdio.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <limits>
#include <mutex>
#include <set>

namespace wgpupixel::text {
namespace {
constexpr std::size_t max_font_bytes = 64 * 1024 * 1024;
constexpr std::size_t max_text_bytes = 1024 * 1024;
constexpr std::size_t max_raster_bytes = 256 * 1024 * 1024;
[[noreturn]] void fail(ErrorCode c, const char* op, const char* arg, const char* msg) {
    throw Error(c, op, arg, msg);
}
void require(bool ok, const char* op, const char* arg, const char* msg) {
    if (!ok) {
        fail(ErrorCode::invalid_argument, op, arg, msg);
    }
}
int units(double x, const char* op, const char* arg) {
    require(std::isfinite(x) && std::abs(x) <= 1000000, op, arg,
            "coordinate must be finite and within +/- 1000000 pixels");
    return static_cast<int>(std::lround(x * PANGO_SCALE));
}
Rect rect(PangoRectangle r) {
    return {double(r.x) / PANGO_SCALE, double(r.y) / PANGO_SCALE, double(r.width) / PANGO_SCALE,
            double(r.height) / PANGO_SCALE};
}
bool boundary(const std::string& s, std::size_t i) {
    return i <= s.size() && (i == s.size() || (static_cast<unsigned char>(s[i]) & 0xc0) != 0x80);
}
void valid_string(const std::string& s, const char* op, const char* arg) {
    require(s.find('\0') == std::string::npos &&
                g_utf8_validate(s.data(), static_cast<gssize>(s.size()), nullptr),
            op, arg, "must be UTF-8 without NUL bytes");
}
struct TempFile {
    std::string path;
    ~TempFile() {
        if (!path.empty()) {
            g_unlink(path.c_str());
        }
    }
};
using Temp = std::unique_ptr<TempFile>;
Temp copy_font(std::span<const std::uint8_t> bytes) {
    require(!bytes.empty() && bytes.size() <= max_font_bytes, "text.add_bytes", "bytes",
            "font data must contain 1 through 67108864 bytes");
    auto temp = std::make_unique<TempFile>();
    gchar* name = nullptr;
    GError* error = nullptr;
    int fd = g_file_open_tmp("wgpupixel-font-XXXXXX", &name, &error);
    if (fd < 0) {
        if (error) {
            g_error_free(error);
        }
        fail(ErrorCode::io_failed, "text.add_bytes", "bytes", "cannot create private font storage");
    }
    try {
        temp->path = name;
    } catch (...) {
        g_close(fd, nullptr);
        g_unlink(name);
        g_free(name);
        throw;
    }
    g_free(name);
#ifdef _WIN32
    FILE* file = _fdopen(fd, "wb");
#else
    FILE* file = fdopen(fd, "wb");
#endif
    if (!file) {
        g_close(fd, nullptr);
        fail(ErrorCode::io_failed, "text.add_bytes", "bytes", "cannot open private font storage");
    }
    bool ok = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    ok = std::fclose(file) == 0 && ok;
    if (!ok) {
        fail(ErrorCode::io_failed, "text.add_bytes", "bytes", "cannot write private font storage");
    }
    return temp;
}
double srgb(double v) {
    v = std::clamp(v, 0.0, 1.0);
    return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}
} // namespace
namespace detail {
struct Collection {
    mutable std::mutex mutex;
    PangoFontMap* map = nullptr;
    std::vector<Temp> files;
    std::set<std::string> families;
    std::size_t layouts = 0;
    Collection() {
        map = pango_cairo_font_map_new_for_font_type(CAIRO_FONT_TYPE_FT);
        if (!map) {
            fail(ErrorCode::execution_failed, "text.FontCollection", "backend",
                 "Pango Cairo FreeType backend is unavailable");
        }
        FcConfig* config = FcConfigCreate();
        if (!config) {
            g_object_unref(map);
            fail(ErrorCode::out_of_memory, "text.FontCollection", "backend",
                 "cannot create font configuration");
        }
        pango_fc_font_map_set_config(PANGO_FC_FONT_MAP(map), config);
        FcConfigDestroy(config);
    }
    ~Collection() {
        if (map) {
            g_object_unref(map);
        }
    }
};
struct LayoutData {
    std::shared_ptr<Collection> collection;
    std::string text;
    PangoLayout* layout = nullptr;
    ~LayoutData() {
        if (collection) {
            std::lock_guard lock(collection->mutex);
            if (layout) {
                g_object_unref(layout);
            }
            --collection->layouts;
        } else if (layout) {
            g_object_unref(layout);
        }
    }
};
} // namespace detail
namespace {
void validate_style(const TextStyle& s, const detail::Collection& c) {
    constexpr const char* op = "text.layout";
    require(s.family.size() <= 4096, op, "family", "family name exceeds 4096 bytes");
    require(s.features.size() <= 4096, op, "features", "feature settings exceed 4096 bytes");
    require(s.variations.size() <= 4096, op, "variations", "variation settings exceed 4096 bytes");
    require(s.language.size() <= 128, op, "language", "language tag exceeds 128 bytes");
    valid_string(s.family, op, "family");
    require(c.families.contains(s.family), op, "family",
            "family must exactly name a registered font family");
    require(s.size_px > 0 && s.size_px <= 16384 && std::isfinite(s.size_px), op, "size_px",
            "font size must be finite and in (0, 16384]");
    require(s.weight >= 1 && s.weight <= 1000, op, "weight", "weight must be in [1, 1000]");
    units(s.letter_spacing_px, op, "letter_spacing_px");
    valid_string(s.features, op, "features");
    valid_string(s.variations, op, "variations");
    valid_string(s.language, op, "language");
    auto validate_settings = [&](const std::string& settings, bool variation) {
        std::size_t start = 0;
        while (start < settings.size()) {
            auto end = settings.find(',', start);
            if (end == std::string::npos) {
                end = settings.size();
            }
            hb_feature_t feature{};
            hb_variation_t axis{};
            bool valid = variation
                             ? hb_variation_from_string(settings.data() + start,
                                                        static_cast<int>(end - start), &axis)
                             : hb_feature_from_string(settings.data() + start,
                                                      static_cast<int>(end - start), &feature);
            require(valid && (!variation || std::isfinite(axis.value)), op,
                    variation ? "variations" : "features",
                    "invalid comma-separated OpenType setting");
            require(end == settings.size() || end + 1 < settings.size(), op,
                    variation ? "variations" : "features", "trailing comma in OpenType settings");
            start = end + 1;
        }
    };
    validate_settings(s.features, false);
    validate_settings(s.variations, true);
    require(std::isfinite(s.color.r) && std::isfinite(s.color.g) && std::isfinite(s.color.b) &&
                std::isfinite(s.color.a) && s.color.a >= 0 && s.color.a <= 1,
            op, "color", "linear premultiplied color must be finite with alpha in [0, 1]");
}
PangoFontDescription* font_description(const TextStyle& s) {
    PangoFontDescription* desc = pango_font_description_new();
    pango_font_description_set_family(desc, s.family.c_str());
    pango_font_description_set_absolute_size(desc, s.size_px * PANGO_SCALE);
    pango_font_description_set_weight(desc, static_cast<PangoWeight>(s.weight));
    pango_font_description_set_style(desc, s.italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
    pango_font_description_set_variations(desc, s.variations.c_str());
    return desc;
}
void style_attributes(PangoAttrList* attrs, const TextStyle& s, guint start, guint end) {
    auto add = [&](PangoAttribute* a) {
        a->start_index = start;
        a->end_index = end;
        pango_attr_list_change(attrs, a);
    };
    auto* desc = font_description(s);
    add(pango_attr_font_desc_new(desc));
    pango_font_description_free(desc);
    add(pango_attr_font_features_new(s.features.c_str()));
    add(pango_attr_language_new(
        pango_language_from_string(s.language.empty() ? "und" : s.language.c_str())));
    auto channel = [&](double v) {
        return static_cast<guint16>(std::lround(srgb(s.color.a > 0 ? v / s.color.a : 0) * 65535));
    };
    add(pango_attr_foreground_new(channel(s.color.r), channel(s.color.g), channel(s.color.b)));
    add(pango_attr_foreground_alpha_new(static_cast<guint16>(std::lround(s.color.a * 65535))));
}
detail::LayoutData& get(const std::shared_ptr<detail::LayoutData>& d) {
    if (!d) {
        fail(ErrorCode::invalid_resource, "text.Layout", "layout", "layout is empty");
    }
    return *d;
}
} // namespace
FontCollection::FontCollection() try : data_(std::make_shared<detail::Collection>()) {
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, "text.FontCollection", "memory", "allocation failed");
}
std::vector<std::string> FontCollection::add_bytes(std::span<const std::uint8_t> bytes) try {
    if (!data_) {
        fail(ErrorCode::invalid_resource, "text.add_bytes", "collection",
             "collection was moved from");
    }
    std::lock_guard lock(data_->mutex);
    if (data_->layouts) {
        fail(ErrorCode::resource_busy, "text.add_bytes", "collection",
             "cannot add fonts while layouts from this collection exist");
    }
    auto file = copy_font(bytes);
    FcConfig* config = FcConfigCreate();
    if (!config) {
        fail(ErrorCode::out_of_memory, "text.add_bytes", "bytes",
             "cannot allocate font configuration");
    }
    std::unique_ptr<FcConfig, decltype(&FcConfigDestroy)> owned(config, FcConfigDestroy);
    for (const auto& f : data_->files) {
        if (!FcConfigAppFontAddFile(config, reinterpret_cast<const FcChar8*>(f->path.c_str()))) {
            fail(ErrorCode::io_failed, "text.add_bytes", "bytes", "cannot reload registered font");
        }
    }
    if (!FcConfigAppFontAddFile(config, reinterpret_cast<const FcChar8*>(file->path.c_str()))) {
        fail(ErrorCode::invalid_argument, "text.add_bytes", "bytes",
             "font data is not supported by Fontconfig/FreeType");
    }
    std::set<std::string> family_set;
    std::set<std::pair<std::string, std::string>> existing_faces;
    std::set<std::pair<std::string, std::string>> new_faces;
    std::vector<std::string> added;
    FcFontSet* fonts = FcConfigGetFonts(config, FcSetApplication);
    for (int i = 0; fonts && i < fonts->nfont; ++i) {
        FcChar8* family = nullptr;
        FcChar8* path = nullptr;
        FcPatternGetString(fonts->fonts[i], FC_FILE, 0, &path);
        FcChar8* primary = nullptr;
        FcChar8* face_style = nullptr;
        if (FcPatternGetString(fonts->fonts[i], FC_FAMILY, 0, &primary) == FcResultMatch &&
            FcPatternGetString(fonts->fonts[i], FC_STYLE, 0, &face_style) == FcResultMatch) {
            auto identity = std::make_pair(std::string(reinterpret_cast<char*>(primary)),
                                           std::string(reinterpret_cast<char*>(face_style)));
            if (path && file->path == reinterpret_cast<char*>(path)) {
                new_faces.insert(std::move(identity));
            } else {
                existing_faces.insert(std::move(identity));
            }
        }
        for (int n = 0; FcPatternGetString(fonts->fonts[i], FC_FAMILY, n, &family) == FcResultMatch;
             ++n) {
            family_set.emplace(reinterpret_cast<char*>(family));
            if (path && file->path == reinterpret_cast<char*>(path)) {
                added.emplace_back(reinterpret_cast<char*>(family));
            }
        }
    }
    for (const auto& identity : new_faces) {
        require(!existing_faces.contains(identity), "text.add_bytes", "bytes",
                "a font with the same family and face style is already registered");
    }
    require(!added.empty(), "text.add_bytes", "bytes", "font has no usable family");
    std::sort(added.begin(), added.end());
    added.erase(std::unique(added.begin(), added.end()), added.end());
    data_->files.push_back(std::move(file));
    data_->families.swap(family_set);
    pango_fc_font_map_set_config(PANGO_FC_FONT_MAP(data_->map), config);
    return added;
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, "text.add_bytes", "bytes", "allocation failed");
}
std::vector<std::string> FontCollection::add_file(std::string_view path) try {
    require(!path.empty(), "text.add_file", "path", "path must not be empty");
    std::string p(path);
    valid_string(p, "text.add_file", "path");
    std::ifstream f(std::filesystem::path(std::u8string(p.begin(), p.end())),
                    std::ios::binary | std::ios::ate);
    if (!f) {
        fail(ErrorCode::io_failed, "text.add_file", "path", "cannot open font file");
    }
    auto length = f.tellg();
    require(length > 0 && length <= static_cast<std::streamoff>(max_font_bytes), "text.add_file",
            "path", "font file must contain 1 through 67108864 bytes");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(bytes.data()), length)) {
        fail(ErrorCode::io_failed, "text.add_file", "path", "cannot read font file");
    }
    return add_bytes(bytes);
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, "text.add_file", "path", "allocation failed");
}
std::vector<std::string> FontCollection::families() const try {
    if (!data_) {
        fail(ErrorCode::invalid_resource, "text.families", "collection",
             "collection was moved from");
    }
    std::lock_guard lock(data_->mutex);
    return {data_->families.begin(), data_->families.end()};
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, "text.families", "memory", "allocation failed");
}
Layout FontCollection::layout(std::string_view utf8, const TextStyle& style,
                              const ParagraphStyle& p, std::span<const StyleRange> ranges) const
    try {
    if (!data_) {
        fail(ErrorCode::invalid_resource, "text.layout", "collection", "collection was moved from");
    }
    std::lock_guard lock(data_->mutex);
    require(utf8.size() <= max_text_bytes, "text.layout", "utf8", "text exceeds 1 MiB limit");
    std::string text(utf8);
    valid_string(text, "text.layout", "utf8");
    validate_style(style, *data_);
    require(p.width_px == -1 || (p.width_px > 0 && std::isfinite(p.width_px)), "text.layout",
            "width_px", "width must be -1 (unbounded) or positive");
    int width = p.width_px == -1 ? -1 : units(p.width_px, "text.layout", "width_px");
    require(p.line_spacing_px >= 0, "text.layout", "line_spacing_px",
            "line spacing must be nonnegative");
    int spacing = units(p.line_spacing_px, "text.layout", "line_spacing_px");
    require(p.wrap == Wrap::word || p.wrap == Wrap::character || p.wrap == Wrap::word_char,
            "text.layout", "wrap", "invalid wrap mode");
    require(p.alignment == Alignment::left || p.alignment == Alignment::center ||
                p.alignment == Alignment::right,
            "text.layout", "alignment", "invalid alignment");
    require(p.direction == Direction::auto_detect || p.direction == Direction::left_to_right ||
                p.direction == Direction::right_to_left,
            "text.layout", "direction", "invalid direction");
    std::size_t previous = 0;
    double largest_size = style.size_px;
    double largest_tracking = std::abs(style.letter_spacing_px);
    for (const auto& r : ranges) {
        require(r.start_byte >= previous && r.start_byte < r.end_byte &&
                    boundary(text, r.start_byte) && boundary(text, r.end_byte),
                "text.layout", "ranges",
                "ranges must be sorted nonoverlapping UTF-8 byte boundaries");
        validate_style(r.style, *data_);
        previous = r.end_byte;
        largest_size = std::max(largest_size, r.style.size_px);
        largest_tracking = std::max(largest_tracking, std::abs(r.style.letter_spacing_px));
    }
    const auto characters = g_utf8_strlen(text.data(), static_cast<gssize>(text.size()));
    // Pango uses signed 32-bit fixed-point coordinates. Reject extreme paragraphs
    // before shaping so pathological wrapping cannot overflow its coordinate range.
    const double coordinate_budget =
        (double(characters) + 1) * (4 * largest_size + largest_tracking + p.line_spacing_px);
    if (coordinate_budget > 1000000) {
        fail(ErrorCode::capacity, "text.layout", "dimensions",
             "text and font size exceed the safe paragraph coordinate budget");
    }
    auto d = std::make_shared<detail::LayoutData>();
    d->text = std::move(text);
    PangoContext* context = pango_font_map_create_context(data_->map);
    pango_cairo_context_set_resolution(context, 96);
    cairo_font_options_t* options = cairo_font_options_create();
    cairo_font_options_set_antialias(options, CAIRO_ANTIALIAS_GRAY);
    cairo_font_options_set_hint_style(options, CAIRO_HINT_STYLE_NONE);
    cairo_font_options_set_hint_metrics(options, CAIRO_HINT_METRICS_OFF);
    pango_cairo_context_set_font_options(context, options);
    cairo_font_options_destroy(options);
    pango_context_set_language(context, pango_language_from_string("und"));
    pango_context_set_base_dir(context, p.direction == Direction::right_to_left
                                            ? PANGO_DIRECTION_RTL
                                            : PANGO_DIRECTION_LTR);
    d->layout = pango_layout_new(context);
    g_object_unref(context);
    auto* base = font_description(style);
    pango_layout_set_font_description(d->layout, base);
    pango_font_description_free(base);
    pango_layout_set_text(d->layout, d->text.data(), static_cast<int>(d->text.size()));
    pango_layout_set_width(d->layout, width);
    pango_layout_set_wrap(d->layout, p.wrap == Wrap::word        ? PANGO_WRAP_WORD
                                     : p.wrap == Wrap::character ? PANGO_WRAP_CHAR
                                                                 : PANGO_WRAP_WORD_CHAR);
    pango_layout_set_alignment(d->layout, p.alignment == Alignment::left     ? PANGO_ALIGN_LEFT
                                          : p.alignment == Alignment::center ? PANGO_ALIGN_CENTER
                                                                             : PANGO_ALIGN_RIGHT);
    pango_layout_set_auto_dir(d->layout, p.direction == Direction::auto_detect);
    pango_layout_set_justify(d->layout, p.justify);
    pango_layout_set_spacing(d->layout, spacing);
    PangoAttrList* attrs = pango_attr_list_new();
    style_attributes(attrs, style, 0, G_MAXUINT);
    for (const auto& r : ranges) {
        style_attributes(attrs, r.style, static_cast<guint>(r.start_byte),
                         static_cast<guint>(r.end_byte));
    }
    // Pango disables optional ligatures whenever a letter-spacing attribute exists,
    // including a zero value. Only attach it to intervals with actual tracking.
    auto tracking = [&](std::size_t begin, std::size_t end, double spacing_px) {
        if (begin >= end || spacing_px == 0) {
            return;
        }
        auto* a =
            pango_attr_letter_spacing_new(units(spacing_px, "text.layout", "letter_spacing_px"));
        a->start_index = static_cast<guint>(begin);
        a->end_index = static_cast<guint>(end);
        pango_attr_list_insert(attrs, a);
    };
    std::size_t begin = 0;
    for (const auto& r : ranges) {
        tracking(begin, r.start_byte, style.letter_spacing_px);
        tracking(r.start_byte, r.end_byte, r.style.letter_spacing_px);
        begin = r.end_byte;
    }
    tracking(begin, d->text.size(), style.letter_spacing_px);
    pango_layout_set_attributes(d->layout, attrs);
    pango_attr_list_unref(attrs);
    d->collection = data_;
    ++data_->layouts;
    return Layout(std::move(d));
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, "text.layout", "memory", "allocation failed");
}
Layout::Layout(std::shared_ptr<detail::LayoutData> d) : data_(std::move(d)) {}
Metrics Layout::metrics() const {
    auto& d = get(data_);
    std::lock_guard lock(d.collection->mutex);
    PangoRectangle ink{}, logical{};
    pango_layout_get_extents(d.layout, &ink, &logical);
    return {rect(ink), rect(logical), double(pango_layout_get_baseline(d.layout)) / PANGO_SCALE,
            pango_layout_get_line_count(d.layout), pango_layout_get_unknown_glyphs_count(d.layout)};
}
HitTest Layout::hit_test(double x, double y) const {
    auto& d = get(data_);
    int px = units(x, "text.hit_test", "position"), py = units(y, "text.hit_test", "position");
    std::lock_guard lock(d.collection->mutex);
    int index = 0, trailing = 0;
    bool inside = pango_layout_xy_to_index(d.layout, px, py, &index, &trailing);
    return {static_cast<std::size_t>(index), trailing, inside};
}
Caret Layout::caret(std::size_t index) const {
    auto& d = get(data_);
    require(boundary(d.text, index), "text.caret", "byte_index",
            "index must be a UTF-8 byte boundary");
    std::lock_guard lock(d.collection->mutex);
    PangoRectangle strong{}, weak{};
    pango_layout_get_cursor_pos(d.layout, static_cast<int>(index), &strong, &weak);
    return {rect(strong), rect(weak)};
}
std::vector<Rect> Layout::selection(std::size_t start, std::size_t end) const try {
    auto& d = get(data_);
    require(start <= end && boundary(d.text, start) && boundary(d.text, end), "text.selection",
            "range", "selection must use ordered UTF-8 byte boundaries");
    std::lock_guard lock(d.collection->mutex);
    std::vector<Rect> result;
    if (start == end) {
        return result;
    }
    std::unique_ptr<PangoLayoutIter, decltype(&pango_layout_iter_free)> iterator(
        pango_layout_get_iter(d.layout), pango_layout_iter_free);
    auto* iter = iterator.get();
    do {
        PangoLayoutLine* line = pango_layout_iter_get_line_readonly(iter);
        if (end <= static_cast<std::size_t>(line->start_index) ||
            start >= static_cast<std::size_t>(line->start_index + line->length)) {
            continue;
        }
        int *ranges = nullptr, count = 0;
        pango_layout_line_get_x_ranges(line, static_cast<int>(start), static_cast<int>(end),
                                       &ranges, &count);
        std::unique_ptr<int, decltype(&g_free)> owned(ranges, g_free);
        int y0 = 0, y1 = 0;
        pango_layout_iter_get_line_yrange(iter, &y0, &y1);
        for (int i = 0; i < count; ++i) {
            result.push_back({double(ranges[2 * i]) / PANGO_SCALE, double(y0) / PANGO_SCALE,
                              double(ranges[2 * i + 1] - ranges[2 * i]) / PANGO_SCALE,
                              double(y1 - y0) / PANGO_SCALE});
        }
    } while (pango_layout_iter_next_line(iter));
    return result;
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, "text.selection", "memory", "allocation failed");
}
Raster Layout::rasterize(RasterFormat format) const try {
    auto& d = get(data_);
    require(format == RasterFormat::a8 || format == RasterFormat::rgba8, "text.rasterize", "format",
            "invalid raster format");
    std::lock_guard lock(d.collection->mutex);
    Raster result;
    result.format = format;
    PangoRectangle ink{};
    pango_layout_get_pixel_extents(d.layout, &ink, nullptr);
    result.origin_x = ink.x;
    result.origin_y = ink.y;
    if (ink.width <= 0 || ink.height <= 0) {
        return result;
    }
    auto bpp = format == RasterFormat::a8 ? 1u : 4u;
    require(ink.width <= 32767 && ink.height <= 32767, "text.rasterize", "dimensions",
            "raster dimensions exceed 32767 pixels");
    std::size_t size = static_cast<std::size_t>(ink.width) * ink.height * bpp;
    if (size > max_raster_bytes) {
        fail(ErrorCode::capacity, "text.rasterize", "dimensions", "raster exceeds 256 MiB");
    }
    result.width = ink.width;
    result.height = ink.height;
    result.stride = result.width * bpp;
    result.pixels.resize(size);
    std::unique_ptr<cairo_surface_t, decltype(&cairo_surface_destroy)> surface(
        cairo_image_surface_create(format == RasterFormat::a8 ? CAIRO_FORMAT_A8
                                                              : CAIRO_FORMAT_ARGB32,
                                   ink.width, ink.height),
        cairo_surface_destroy);
    if (cairo_surface_status(surface.get()) != CAIRO_STATUS_SUCCESS) {
        fail(ErrorCode::out_of_memory, "text.rasterize", "surface",
             "cannot allocate Cairo surface");
    }
    std::unique_ptr<cairo_t, decltype(&cairo_destroy)> cr(cairo_create(surface.get()),
                                                          cairo_destroy);
    cairo_translate(cr.get(), -ink.x, -ink.y);
    // Both formats reuse the shaped runs. A8 ignores style color and opacity;
    // RGBA isolates translucent runs so opacity also applies to intrinsic-color glyphs.
    std::unique_ptr<PangoLayoutIter, decltype(&pango_layout_iter_free)> iterator(
        pango_layout_get_iter(d.layout), pango_layout_iter_free);
    do {
        auto* run = pango_layout_iter_get_run_readonly(iterator.get());
        if (!run) {
            continue;
        }
        double red = 1, green = 1, blue = 1, opacity = 1;
        if (format == RasterFormat::rgba8) {
            for (GSList* node = run->item->analysis.extra_attrs; node; node = node->next) {
                const auto* attr = static_cast<PangoAttribute*>(node->data);
                if (attr->klass->type == PANGO_ATTR_FOREGROUND) {
                    const auto& color = reinterpret_cast<const PangoAttrColor*>(attr)->color;
                    red = double(color.red) / 65535;
                    green = double(color.green) / 65535;
                    blue = double(color.blue) / 65535;
                } else if (attr->klass->type == PANGO_ATTR_FOREGROUND_ALPHA) {
                    opacity = double(reinterpret_cast<const PangoAttrInt*>(attr)->value) / 65535;
                }
            }
        }
        if (opacity == 0) {
            continue;
        }
        PangoRectangle logical{};
        pango_layout_iter_get_run_extents(iterator.get(), nullptr, &logical);
        if (opacity < 1) {
            cairo_push_group(cr.get());
        }
        cairo_set_source_rgb(cr.get(), red, green, blue);
        cairo_move_to(cr.get(), double(logical.x) / PANGO_SCALE,
                      double(pango_layout_iter_get_run_baseline(iterator.get())) / PANGO_SCALE);
        pango_cairo_show_glyph_item(cr.get(), d.text.c_str(), run);
        if (opacity < 1) {
            cairo_pop_group_to_source(cr.get());
            cairo_paint_with_alpha(cr.get(), opacity);
        }
    } while (pango_layout_iter_next_run(iterator.get()));
    cairo_surface_flush(surface.get());
    if (cairo_status(cr.get()) != CAIRO_STATUS_SUCCESS) {
        fail(ErrorCode::execution_failed, "text.rasterize", "surface", "Cairo rendering failed");
    }
    const auto* pixels = cairo_image_surface_get_data(surface.get());
    int stride = cairo_image_surface_get_stride(surface.get());
    for (std::uint32_t y = 0; y < result.height; ++y) {
        auto* dst = result.pixels.data() + static_cast<std::size_t>(y) * result.stride;
        const auto* src = pixels + static_cast<std::size_t>(y) * stride;
        if (format == RasterFormat::a8) {
            std::memcpy(dst, src, result.width);
        } else {
            for (std::uint32_t x = 0; x < result.width; ++x) {
                std::uint32_t argb;
                std::memcpy(&argb, src + 4 * x, 4);
                unsigned a = argb >> 24;
                dst[4 * x + 3] = static_cast<std::uint8_t>(a);
                for (unsigned c = 0; c < 3; ++c) {
                    unsigned v = (argb >> (16 - 8 * c)) & 255;
                    dst[4 * x + c] =
                        a ? static_cast<std::uint8_t>(std::min(255u, (v * 255 + a / 2) / a)) : 0;
                }
            }
        }
    }
    return result;
} catch (const std::bad_alloc&) {
    fail(ErrorCode::out_of_memory, "text.rasterize", "memory", "allocation failed");
}
} // namespace wgpupixel::text
