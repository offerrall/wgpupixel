#include "test.h"
#include <wgpupixel_text.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>

using namespace wgpupixel;
namespace tx = wgpupixel::text;
namespace {
std::string fixture(std::string_view name = "NotoSans-Regular.ttf") {
    return (std::filesystem::path(WGPUPIXEL_TEST_DATA) / "fonts" / name).string();
}
std::vector<std::uint8_t> font_bytes() {
    std::ifstream input(fixture(), std::ios::binary);
    test::check(bool(input), "font fixture missing");
    return {std::istreambuf_iterator<char>(input), {}};
}
tx::TextStyle style() {
    tx::TextStyle value;
    value.family = "Noto Sans";
    value.size_px = 32;
    return value;
}
tx::FontCollection fonts(bool arabic = false) {
    tx::FontCollection value;
    (void)value.add_file(fixture());
    if (arabic) {
        (void)value.add_file(fixture("NotoSansArabic-Regular.ttf"));
    }
    return value;
}
template <class Function> void fails(ErrorCode code, Function function) {
    try {
        function();
    } catch (const Error& error) {
        test::check(error.code() == code, "wrong error code");
        return;
    }
    throw std::runtime_error("expected typography error");
}
void packed(const tx::Raster& raster, tx::RasterFormat format) {
    test::check(raster.format == format, "wrong raster format");
    test::check(raster.stride == raster.width * (format == tx::RasterFormat::a8 ? 1 : 4),
                "padded rows leaked");
    test::check(raster.pixels.size() == std::size_t(raster.stride) * raster.height,
                "wrong raster storage");
}
} // namespace
int main() {
    test::run("private file and copied memory fonts produce identical output", [] {
        auto file = fonts();
        tx::FontCollection memory;
        {
            auto bytes = font_bytes();
            (void)memory.add_bytes(bytes);
            std::fill(bytes.begin(), bytes.end(), 0);
        }
        test::check(file.families() == memory.families(), "file/bytes family metadata differs");
        test::check(file.families() == std::vector<std::string>{"Noto Sans"},
                    "system fonts leaked into private collection");
        auto a = file.layout("office café", style()).rasterize();
        auto b = memory.layout("office café", style()).rasterize();
        test::check(a.width == b.width && a.height == b.height && a.pixels == b.pixels,
                    "copied font storage changed output");
        tx::FontCollection empty;
        test::check(empty.families().empty(), "new collection contains host fonts");
        fails(ErrorCode::invalid_argument, [&] { (void)empty.layout("A", style()); });
    });
    test::run("registered file survives source deletion", [] {
        const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
        auto path = std::filesystem::temp_directory_path() /
                    ("wgpupixel-text-test-" + std::to_string(seed) + ".ttf");
        struct Remove {
            std::filesystem::path path;
            ~Remove() {
                std::error_code error;
                std::filesystem::remove(path, error);
            }
        } cleanup{path};
        std::filesystem::copy_file(fixture(), path);
        tx::FontCollection collection;
        (void)collection.add_file(path.string());
        std::filesystem::remove(path);
        test::check(!collection.layout("Deleted source", style()).rasterize().pixels.empty(),
                    "font registration retained original path");
    });
    test::run("layout owns fonts after collection lifetime and copies share layout", [] {
        tx::Layout layout;
        {
            auto collection = fonts();
            layout = collection.layout("Retained", style());
        }
        auto copy = layout;
        layout = {};
        test::check(copy.metrics().unknown_glyphs == 0, "font backing lost with collection");
        test::check(!copy.rasterize().pixels.empty(), "retained layout lost raster");
        fails(ErrorCode::invalid_resource, [&] { (void)layout.metrics(); });
    });
    test::run("font mutation is rejected while layouts exist", [] {
        auto collection = fonts();
        {
            auto layout = collection.layout("A", style());
            fails(ErrorCode::resource_busy,
                  [&] { (void)collection.add_file(fixture("NotoSansArabic-Regular.ttf")); });
        }
        (void)collection.add_file(fixture("NotoSansArabic-Regular.ttf"));
        test::check(collection.families().size() == 2, "released layouts still lock collection");
    });
    test::run("duplicate faces reject without changing font registry", [] {
        auto collection = fonts();
        test::error(ErrorCode::invalid_argument, "text.add_bytes", "bytes",
                    [&] { (void)collection.add_bytes(font_bytes()); });
        test::check(collection.families() == std::vector<std::string>{"Noto Sans"},
                    "duplicate changed family registry");
        test::check(collection.layout("still valid", style()).metrics().unknown_glyphs == 0,
                    "duplicate invalidated font map");
    });
    test::run("malformed fonts and paths reject before registration", [] {
        tx::FontCollection collection;
        const std::array<std::uint8_t, 8> garbage{0, 1, 2, 3, 4, 5, 6, 7};
        fails(ErrorCode::invalid_argument, [&] { (void)collection.add_bytes({}); });
        fails(ErrorCode::invalid_argument, [&] { (void)collection.add_bytes(garbage); });
        test::error(ErrorCode::invalid_argument, "text.add_file", "path",
                    [&] { (void)collection.add_file(""); });
        fails(ErrorCode::io_failed, [&] { (void)collection.add_file(fixture("missing.ttf")); });
        test::check(collection.families().empty(), "failed registration modified collection");
        (void)collection.add_file(fixture());
        test::check(collection.layout("A", style()).metrics().unknown_glyphs == 0,
                    "failed registration poisoned collection");
    });
    test::run("empty and whitespace layouts retain geometry without allocating ink", [] {
        auto collection = fonts();
        for (auto value : {"", "   ", "\n"}) {
            auto layout = collection.layout(value, style());
            const auto raster = layout.rasterize(tx::RasterFormat::a8);
            packed(raster, tx::RasterFormat::a8);
            test::check(raster.width == 0 && raster.height == 0 && raster.pixels.empty(),
                        "empty ink allocates a bitmap");
        }
        auto small = style();
        small.size_px = 12;
        auto large = style();
        large.size_px = 48;
        test::check(collection.layout("", large).metrics().logical.height >
                        collection.layout("", small).metrics().logical.height,
                    "empty layout ignores requested font metrics");
        test::check(collection.layout("   ", style()).metrics().logical.width > 0,
                    "spaces lost advance");
        test::check(collection.layout("\n", style()).metrics().line_count == 2,
                    "newline lost paragraph break");
    });
    test::run("raster bounds enclose negative bearings and accents", [] {
        auto collection = fonts();
        auto settings = style();
        settings.italic = true;
        auto layout = collection.layout("jÁg", settings);
        const auto m = layout.metrics();
        const auto a = layout.rasterize(tx::RasterFormat::a8);
        packed(a, tx::RasterFormat::a8);
        test::check(m.baseline_px > 0 && m.logical.height > 0 && m.unknown_glyphs == 0,
                    "invalid text metrics");
        test::check(a.origin_x <= m.ink.x && a.origin_y <= m.ink.y, "raster clips leading ink");
        test::check(a.origin_x + double(a.width) >= m.ink.x + m.ink.width &&
                        a.origin_y + double(a.height) >= m.ink.y + m.ink.height,
                    "raster clips trailing ink");
        test::check(std::any_of(a.pixels.begin(), a.pixels.end(), [](auto c) { return c > 0; }),
                    "raster contains no ink");
    });
    test::run("wrap alignment spacing and explicit line breaks", [] {
        auto collection = fonts();
        auto settings = style();
        auto plain = collection.layout("one two three four", settings);
        tx::ParagraphStyle p;
        p.width_px = 100;
        auto wrapped = collection.layout("one two three four", settings, p);
        test::check(wrapped.metrics().line_count > 1 &&
                        wrapped.metrics().logical.height > plain.metrics().logical.height,
                    "paragraph did not wrap");
        p.width_px = 300;
        auto left = collection.layout("one", settings, p);
        p.alignment = tx::Alignment::right;
        auto right = collection.layout("one", settings, p);
        test::check(right.metrics().logical.x > left.metrics().logical.x,
                    "right alignment ignored");
        settings.letter_spacing_px = 3;
        test::check(collection.layout("one two three four", settings).metrics().logical.width >
                        plain.metrics().logical.width,
                    "tracking ignored");
        auto lines = collection.layout("one\ntwo", style());
        p.line_spacing_px = 12;
        test::check(collection.layout("one\ntwo", style(), p).metrics().logical.height >
                        lines.metrics().logical.height,
                    "line spacing ignored");
    });
    test::run("Arabic fallback bidi grapheme carets and selections", [] {
        auto collection = fonts(true);
        const std::string value = "abc العربية xyz";
        auto layout = collection.layout(value, style());
        test::check(layout.metrics().unknown_glyphs == 0, "registered Arabic fallback not used");
        const auto boxes = layout.selection(0, value.size());
        test::check(!boxes.empty(), "selection has no rectangles");
        for (auto box : boxes) {
            test::check(box.width >= 0 && box.height > 0, "invalid selection geometry");
        }
        auto c = layout.caret(0);
        test::check(c.strong.height > 0, "caret lost line height");
        auto hit = layout.hit_test(c.strong.x + 0.1, c.strong.y + c.strong.height / 2);
        test::check(hit.byte_index == 0, "caret/hit-test disagree at start");
        test::check(!layout.hit_test(-10000, -10000).inside, "outside hit marked inside");
        auto grapheme = collection.layout("e\xCC\x81", style());
        test::check(grapheme.metrics().unknown_glyphs == 0, "combining mark not shaped");
        fails(ErrorCode::invalid_argument, [&] { (void)grapheme.caret(2); });
        fails(ErrorCode::invalid_argument, [&] { (void)layout.selection(4, 5); });
        test::check(layout.selection(0, 0).empty(), "empty selection emitted rectangles");
        tx::ParagraphStyle rtl;
        rtl.direction = tx::Direction::right_to_left;
        auto arabic = collection.layout("العربية", style(), rtl);
        test::check(arabic.caret(0).strong.x > arabic.caret(std::string("العربية").size()).strong.x,
                    "RTL carets not reversed");
    });
    test::run("OpenType ligatures affect editing geometry", [] {
        auto collection = fonts();
        auto settings = style();
        settings.features = "liga=0";
        auto separate = collection.layout("ffi", settings);
        settings.features = "liga=1";
        auto ligature = collection.layout("ffi", settings);
        test::check(separate.rasterize().pixels != ligature.rasterize().pixels,
                    "liga setting did not affect shaping");
        test::check(ligature.caret(1).strong.x > ligature.caret(0).strong.x,
                    "ligature caret cannot advance inside cluster");
        auto tracked = settings;
        tracked.letter_spacing_px = 3;
        const std::array<tx::StyleRange, 1> ranges{{{0, 3, settings}}};
        auto reset = collection.layout("ffi", tracked, {}, ranges);
        test::check(reset.rasterize().pixels == ligature.rasterize().pixels,
                    "replacement range failed to reset tracking and restore ligatures");
    });
    test::run("variable font axes change geometry without changing registered faces", [] {
        tx::FontCollection collection;
        (void)collection.add_file(fixture("AmstelvarAlpha-VF.ttf"));
        auto settings = style();
        settings.family = "AmstelvarAlpha";
        settings.variations = "wdth=60,wght=38";
        auto narrow = collection.layout("Variable", settings);
        settings.variations = "wdth=402,wght=250";
        auto wide = collection.layout("Variable", settings);
        test::check(narrow.metrics().unknown_glyphs == 0 && wide.metrics().unknown_glyphs == 0,
                    "variable font failed to resolve");
        test::check(wide.metrics().logical.width > narrow.metrics().logical.width,
                    "width axis did not change advances");
        test::check(wide.rasterize().pixels != narrow.rasterize().pixels,
                    "variation axis did not change outlines");
    });
    test::run("styled UTF8 ranges preserve base style and output colors", [] {
        auto collection = fonts();
        auto base = style();
        base.color = {1, 0, 0, 1};
        auto green = base;
        green.color = {0, 1, 0, 1};
        green.size_px = 48;
        const std::array<tx::StyleRange, 1> ranges{{{1, 2, green}}};
        auto layout = collection.layout("AB", base, {}, ranges);
        test::check(layout.metrics().logical.height >
                        collection.layout("AB", base).metrics().logical.height,
                    "range size ignored");
        const auto rgba = layout.rasterize();
        packed(rgba, tx::RasterFormat::rgba8);
        bool red = false, green_pixel = false;
        for (std::size_t i = 0; i < rgba.pixels.size(); i += 4) {
            if (rgba.pixels[i + 3] > 32) {
                red |= rgba.pixels[i] > 200 && rgba.pixels[i + 1] < 5;
                green_pixel |= rgba.pixels[i + 1] > 200 && rgba.pixels[i] < 5;
            }
        }
        test::check(red && green_pixel, "range color replaced entire layout or was ignored");
    });
    test::run("A8 is color-independent coverage and RGBA is straight-alpha sRGB", [] {
        auto collection = fonts();
        auto settings = style();
        const auto white = collection.layout("Alpha", settings).rasterize(tx::RasterFormat::a8);
        settings.color = {0.125f, 0.25f, 0.5f, 0.5f};
        auto layout = collection.layout("Alpha", settings);
        const auto mask = layout.rasterize(tx::RasterFormat::a8);
        const auto rgba = layout.rasterize();
        packed(mask, tx::RasterFormat::a8);
        packed(rgba, tx::RasterFormat::rgba8);
        test::check(layout.rasterize(tx::RasterFormat::a8).pixels == mask.pixels &&
                        layout.rasterize().pixels == rgba.pixels,
                    "switching raster formats changed the layout");
        test::check(mask.pixels == white.pixels, "A8 depends on style alpha or color");
        test::check(mask.width == rgba.width && mask.height == rgba.height,
                    "formats disagree on geometry");
        for (std::size_t i = 0; i < mask.pixels.size(); ++i) {
            test::near(rgba.pixels[4 * i + 3], mask.pixels[i] * 0.5f, 2);
            if (rgba.pixels[4 * i + 3] > 100) {
                test::near(rgba.pixels[4 * i], test::channel(0.25f), 3);
                test::near(rgba.pixels[4 * i + 1], test::channel(0.5f), 3);
                test::near(rgba.pixels[4 * i + 2], 255, 3);
            }
        }
    });
    test::run("RGBA shaped run positions match A8 for bidi styles tracking and wrapping", [] {
        auto collection = fonts(true);
        auto base = style();
        base.italic = true;
        base.letter_spacing_px = 1.5;
        auto replacement = base;
        replacement.size_px = 42;
        replacement.letter_spacing_px = 0;
        const std::string value = "jÁ ffi العربية xyz more text";
        const std::array<tx::StyleRange, 1> ranges{{{0, 3, replacement}}};
        tx::ParagraphStyle p;
        p.width_px = 180;
        p.line_spacing_px = 8;
        auto layout = collection.layout(value, base, p, ranges);
        const auto mask = layout.rasterize(tx::RasterFormat::a8);
        const auto rgba = layout.rasterize();
        test::check(mask.width == rgba.width && mask.height == rgba.height &&
                        mask.origin_x == rgba.origin_x && mask.origin_y == rgba.origin_y,
                    "RGBA run placement changed layout bounds");
        for (std::size_t i = 0; i < mask.pixels.size(); ++i) {
            test::near(rgba.pixels[i * 4 + 3], mask.pixels[i], 2);
        }
    });
    test::run("COLRv1 retains multicolor glyphs and A8 ignores style alpha", [] {
        tx::FontCollection collection;
        (void)collection.add_file(fixture("test_glyphs-glyf_colr_1.ttf"));
        auto settings = style();
        settings.family = "COLRv1 Static Test Glyphs";
        settings.size_px = 64;
        // U+F0100 is the upstream synthetic linear-gradient test glyph.
        const std::string glyph = "\xf3\xb0\x84\x80";
        auto opaque = collection.layout(glyph, settings);
        test::check(opaque.metrics().unknown_glyphs == 0, "color glyph failed to resolve");
        const auto rgba = opaque.rasterize();
        std::set<std::uint32_t> colors;
        for (std::size_t i = 0; i < rgba.pixels.size(); i += 4) {
            if (rgba.pixels[i + 3] > 200) {
                colors.insert((std::uint32_t(rgba.pixels[i]) << 16) |
                              (std::uint32_t(rgba.pixels[i + 1]) << 8) | rgba.pixels[i + 2]);
            }
        }
        test::check(colors.size() > 2, "COLRv1 gradient flattened to monochrome");
        const auto coverage = opaque.rasterize(tx::RasterFormat::a8);
        settings.color = {0.25f, 0, 0, 0.25f};
        auto translucent = collection.layout(glyph, settings);
        test::check(translucent.rasterize(tx::RasterFormat::a8).pixels == coverage.pixels,
                    "color glyph A8 depends on style color or alpha");
        const auto faded = translucent.rasterize();
        test::check(faded.width == rgba.width && faded.height == rgba.height,
                    "color opacity changed raster geometry");
        for (std::size_t i = 3; i < rgba.pixels.size(); i += 4) {
            test::near(faded.pixels[i], rgba.pixels[i] * 0.25f, 2);
        }
        settings.color = {0, 0, 0, 0};
        auto transparent = collection.layout(glyph, settings);
        const auto invisible = transparent.rasterize();
        test::check(std::all_of(invisible.pixels.begin(), invisible.pixels.end(),
                                [](auto value) { return value == 0; }),
                    "transparent intrinsic-color glyph remained visible");
        test::check(transparent.rasterize(tx::RasterFormat::a8).pixels == coverage.pixels,
                    "transparent style removed glyph coverage");
    });
    test::run("unregistered fallback is reported and enums/settings reject invalid values", [] {
        auto collection = fonts();
        test::check(collection.layout("العربية", style()).metrics().unknown_glyphs > 0,
                    "unregistered system Arabic font escaped collection isolation");
        auto settings = style();
        settings.features = "not-a-feature";
        test::error(ErrorCode::invalid_argument, "text.layout", "features",
                    [&] { (void)collection.layout("A", settings); });
        settings = style();
        settings.variations = "wght=nan";
        test::error(ErrorCode::invalid_argument, "text.layout", "variations",
                    [&] { (void)collection.layout("A", settings); });
        settings = style();
        settings.weight = 1001;
        test::error(ErrorCode::invalid_argument, "text.layout", "weight",
                    [&] { (void)collection.layout("A", settings); });
        settings = style();
        settings.color.a = -0.5f;
        test::error(ErrorCode::invalid_argument, "text.layout", "color",
                    [&] { (void)collection.layout("A", settings); });
        tx::ParagraphStyle p;
        p.wrap = static_cast<tx::Wrap>(999);
        test::error(ErrorCode::invalid_argument, "text.layout", "wrap",
                    [&] { (void)collection.layout("A", style(), p); });
        auto layout = collection.layout("A", style());
        test::error(ErrorCode::invalid_argument, "text.rasterize", "format",
                    [&] { (void)layout.rasterize(static_cast<tx::RasterFormat>(999)); });
    });
    test::run("excessive layout coordinates reject before backend shaping", [] {
        auto collection = fonts();
        const std::string huge(100000, 'A');
        fails(ErrorCode::capacity, [&] { (void)collection.layout(huge, style()); });
        test::check(collection.layout("Still usable", style()).metrics().unknown_glyphs == 0,
                    "coordinate rejection poisoned collection");
    });
    test::run("UTF8 and numerical validation has structured errors", [] {
        auto collection = fonts();
        for (const auto& value :
             {std::string("\xC0\xAF"), std::string("\xED\xA0\x80"), std::string("A\0B", 3)}) {
            test::error(ErrorCode::invalid_argument, "text.layout", "utf8",
                        [&] { (void)collection.layout(value, style()); });
        }
        for (auto size : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                          std::numeric_limits<double>::quiet_NaN()}) {
            auto settings = style();
            settings.size_px = size;
            test::error(ErrorCode::invalid_argument, "text.layout", "size_px",
                        [&] { (void)collection.layout("A", settings); });
        }
        auto settings = style();
        settings.family = "Unregistered Font";
        test::error(ErrorCode::invalid_argument, "text.layout", "family",
                    [&] { (void)collection.layout("A", settings); });
        tx::ParagraphStyle p;
        p.width_px = std::numeric_limits<double>::quiet_NaN();
        test::error(ErrorCode::invalid_argument, "text.layout", "width_px",
                    [&] { (void)collection.layout("A", style(), p); });
        auto layout = collection.layout("A", style());
        test::error(ErrorCode::invalid_argument, "text.caret", "byte_index",
                    [&] { (void)layout.caret(2); });
        test::error(ErrorCode::invalid_argument, "text.hit_test", "position",
                    [&] { (void)layout.hit_test(0, std::numeric_limits<double>::infinity()); });
        const std::array<tx::StyleRange, 2> ranges{{{0, 2, style()}, {1, 2, style()}}};
        test::error(ErrorCode::invalid_argument, "text.layout", "ranges",
                    [&] { (void)collection.layout("AB", style(), {}, ranges); });
    });
    return test::finish();
}
