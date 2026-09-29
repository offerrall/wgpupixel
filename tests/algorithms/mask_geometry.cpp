#include "../geometry.h"
#include <functional>
#include <limits>

using namespace wgpupixel;
using geometry::Pixels;
using Bytes = std::vector<std::uint8_t>;

namespace {
constexpr std::array filters{ResizeFilter::nearest, ResizeFilter::bilinear, ResizeFilter::bicubic,
                             ResizeFilter::lanczos, ResizeFilter::area};
constexpr std::array edges{EdgeMode::transparent, EdgeMode::clamp, EdgeMode::repeat,
                           EdgeMode::mirror};

Bytes coverage_pattern(int width, int height, int seed = 0) {
    Bytes bytes(std::size_t(width) * height);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = std::uint8_t((i * 97 + seed * 31 + (i % 5) * 60) % 256);
    }
    return bytes;
}

std::uint8_t quantize(float value) {
    return std::uint8_t(std::floor(std::clamp(value, 0.0f, 1.0f) * 255 + 0.5f));
}

// A layer mask and the matching alpha image must move identically.
struct Pair {
    Context& ctx;
    Mask mask_source, mask_destination;
    Image image_source, image_destination;
    Bytes bytes;

    Pair(Context& context, int sw, int sh, int dw, int dh, const Bytes& coverage)
        : ctx(context), mask_source(ctx.create_mask({sw, sh})),
          mask_destination(ctx.create_mask({dw, dh})), image_source(ctx.create_image({sw, sh})),
          image_destination(ctx.create_image({dw, dh})), bytes(coverage) {
        geometry::upload(ctx, mask_source, bytes);
        Pixels pixels;
        for (auto byte : bytes) {
            const float c = byte / 255.0f;
            pixels.insert(pixels.end(), {c, c, c, c});
        }
        geometry::upload(ctx, image_source, pixels);
    }
    ~Pair() {
        ctx.destroy(mask_source);
        ctx.destroy(mask_destination);
        ctx.destroy(image_source);
        ctx.destroy(image_destination);
    }
    void compare(const std::function<void(Commands&)>& record, int tolerance, std::string what) {
        auto cmd = ctx.create_commands(64);
        cmd.fill(mask_destination, {.coverage = 0.5f});
        cmd.fill(image_destination, {.color = {0.5f, 0.5f, 0.5f, 0.5f}});
        record(cmd);
        ctx.submit_and_wait(cmd);
        const auto actual = geometry::download(ctx, mask_destination);
        const auto image = geometry::download(ctx, image_destination);
        for (std::size_t i = 0; i < actual.size(); ++i) {
            const int expected = quantize(image[i * 4 + 3]);
            test::check(std::abs(int(actual[i]) - expected) <= tolerance,
                        what + ": coverage " + std::to_string(i) + " got " +
                            std::to_string(actual[i]) + ", expected " + std::to_string(expected));
        }
    }
};
} // namespace

int main() {
    test::run("mask transform and perspective follow the alpha of the image result", [] {
        auto ctx = Context::create();
        Pair pair(ctx, 9, 7, 11, 6, coverage_pattern(9, 7));
        const Affine matrix = Affine::translate(1.5f, -0.5f) * Affine::rotate(23, {4.5f, 3.5f}) *
                              Affine::scale(0.6f, 1.2f);
        const std::array corners{Point{1, 0.5f}, Point{10.5f, 1.5f}, Point{8, 6}, Point{2.5f, 5}};
        for (auto filter : filters) {
            for (auto edge : edges) {
                const TransformOptions options{.matrix = matrix, .filter = filter, .edge = edge};
                pair.compare(
                    [&](Commands& cmd) {
                        const auto sized = test::sized(ctx, pair.mask_source.size(),
                                                       pair.mask_destination.size(), options);
                        cmd.transform(pair.mask_source, pair.mask_destination, sized);
                        cmd.transform(pair.image_source, pair.image_destination, sized);
                    },
                    // Lanczos/bicubic may ring slightly outside [0, 1] before rounding.
                    1, "transform");
                const PerspectiveOptions projective{
                    .corners = corners, .filter = filter, .edge = edge};
                pair.compare(
                    [&](Commands& cmd) {
                        const auto sized = test::sized(ctx, pair.mask_source.size(),
                                                       pair.mask_destination.size(), projective);
                        cmd.perspective(pair.mask_source, pair.mask_destination, sized);
                        cmd.perspective(pair.image_source, pair.image_destination, sized);
                    },
                    1, "perspective");
            }
        }
    });

    test::run("mask resize and rotate match image resize and rotate", [] {
        auto ctx = Context::create();
        for (const auto size : {std::array{13, 5, 5, 3}, std::array{3, 4, 10, 9},
                                std::array{7, 7, 7, 7}, std::array{21, 3, 6, 11}}) {
            Pair pair(ctx, size[0], size[1], size[2], size[3],
                      coverage_pattern(size[0], size[1], 1));
            for (auto filter : filters) {
                pair.compare(
                    [&](Commands& cmd) {
                        cmd.resize(pair.mask_source, pair.mask_destination, {.filter = filter});
                        cmd.resize(pair.image_source, pair.image_destination, {.filter = filter});
                    },
                    // Nearest copies bytes exactly.
                    filter == ResizeFilter::nearest ? 0 : 1, "resize");
                if (filter == ResizeFilter::area) {
                    continue;
                }
                for (float degrees : {90.f, 180.f, -37.f, 215.5f}) {
                    pair.compare(
                        [&](Commands& cmd) {
                            cmd.rotate(pair.mask_source, pair.mask_destination,
                                       {.degrees = degrees, .filter = filter});
                            cmd.rotate(pair.image_source, pair.image_destination,
                                       {.degrees = degrees, .filter = filter});
                        },
                        1, "rotate");
                }
            }
        }
    });

    test::run("mask crop, flip and offset move bytes exactly", [] {
        auto ctx = Context::create();
        const int sw = 7, sh = 5;
        const auto bytes = coverage_pattern(sw, sh, 2);
        auto source = ctx.create_mask({sw, sh});
        geometry::upload(ctx, source, bytes);
        const auto check = [&](const Mask& destination, auto expected, std::string what) {
            const auto actual = geometry::download(ctx, destination);
            const auto width = int(destination.size().width);
            for (std::size_t i = 0; i < actual.size(); ++i) {
                const int value = expected(int(i) % width, int(i) / width);
                test::check(actual[i] == value, what + " differs at " + std::to_string(i));
            }
        };
        const auto at = [&](long long x, long long y, EdgeMode edge) -> int {
            const int ix = geometry::edge_index(x, sw, edge),
                      iy = geometry::edge_index(y, sh, edge);
            return ix < 0 || iy < 0 ? 0 : bytes[iy * sw + ix];
        };
        constexpr auto low = std::numeric_limits<std::int32_t>::min();
        constexpr auto high = std::numeric_limits<std::int32_t>::max();
        for (const auto size : {std::array{5, 3}, std::array{9, 6}, std::array{1, 1}}) {
            auto destination = ctx.create_mask({size[0], size[1]});
            for (auto origin : {Position{1, 1}, Position{-2, -1}, Position{6, 4}, Position{low, 0},
                                Position{high, low}}) {
                auto cmd = ctx.create_commands(2);
                cmd.fill(destination, {.coverage = 1});
                cmd.crop(source, destination, {.origin = origin});
                ctx.submit_and_wait(cmd);
                check(
                    destination,
                    [&](int x, int y) {
                        return at(x + (long long)origin.x, y + (long long)origin.y,
                                  EdgeMode::transparent);
                    },
                    "crop");
            }
            for (auto edge : edges) {
                for (auto offset :
                     {Position{2, -1}, Position{-9, 13}, Position{low, high}, Position{high, -3}}) {
                    auto cmd = ctx.create_commands(2);
                    cmd.fill(destination, {.coverage = 0.25f});
                    cmd.offset(source, destination, {.offset = offset, .edge = edge});
                    ctx.submit_and_wait(cmd);
                    check(
                        destination,
                        [&](int x, int y) {
                            return at(x - (long long)offset.x, y - (long long)offset.y, edge);
                        },
                        "offset");
                }
            }
            ctx.destroy(destination);
        }
        auto flipped = ctx.create_mask({sw, sh});
        for (auto direction :
             {FlipDirection::horizontal, FlipDirection::vertical, FlipDirection::both}) {
            auto cmd = ctx.create_commands(1);
            cmd.flip(source, flipped, {.direction = direction});
            ctx.submit_and_wait(cmd);
            check(
                flipped,
                [&](int x, int y) {
                    return at(direction != FlipDirection::vertical ? sw - 1 - x : x,
                              direction != FlipDirection::horizontal ? sh - 1 - y : y,
                              EdgeMode::transparent);
                },
                "flip");
        }
    });

    test::run("selection masks and regions limit mask geometry", [] {
        auto ctx = Context::create();
        const int w = 6, h = 5;
        const auto bytes = coverage_pattern(w, h, 3);
        auto source = ctx.create_mask({w, h});
        auto full = ctx.create_mask({w, h});
        auto partial = ctx.create_mask({w, h});
        auto selection = ctx.create_mask({w, h});
        geometry::upload(ctx, source, bytes);
        const auto chosen = coverage_pattern(w, h, 7);
        geometry::upload(ctx, selection, chosen);
        const Bytes before(std::size_t(w) * h, 77);
        using Apply = std::function<void(Commands&, const Mask&, const Mask*, std::optional<Rect>)>;
        const std::vector<std::pair<const char*, Apply>> operations{
            {"transform",
             [&](auto& c, auto& d, auto m, auto r) {
                 c.transform(source, d,
                             {.matrix = Affine::rotate(40, {3, 2.5f}),
                              .filter = ResizeFilter::bilinear,
                              .mask = m,
                              .region = r});
             }},
            {"perspective",
             [&](auto& c, auto& d, auto m, auto r) {
                 c.perspective(source, d,
                               {.corners = {Point{0, 1}, Point{6, 0}, Point{5, 5}, Point{1, 4}},
                                .mask = m,
                                .region = r});
             }},
            {"offset",
             [&](auto& c, auto& d, auto m, auto r) {
                 c.offset(source, d, {.offset = {2, 1}, .mask = m, .region = r});
             }},
            {"resize",
             [&](auto& c, auto& d, auto m, auto r) {
                 c.resize(source, d, {.filter = ResizeFilter::area, .mask = m, .region = r});
             }},
            {"crop",
             [&](auto& c, auto& d, auto m, auto r) {
                 c.crop(source, d, {.origin = {1, -1}, .mask = m, .region = r});
             }},
            {"flip",
             [&](auto& c, auto& d, auto m, auto r) {
                 c.flip(source, d, {.direction = FlipDirection::both, .mask = m, .region = r});
             }},
            {"rotate",
             [&](auto& c, auto& d, auto m, auto r) {
                 c.rotate(source, d, {.degrees = 90, .mask = m, .region = r});
             }},
        };
        const Rect region{1, 2, 4, 9};
        for (const auto& [name, apply] : operations) {
            geometry::upload(ctx, partial, before);
            auto cmd = ctx.create_commands(2);
            apply(cmd, full, nullptr, std::nullopt);
            apply(cmd, partial, &selection, region);
            ctx.submit_and_wait(cmd);
            const auto after = geometry::download(ctx, full);
            const auto actual = geometry::download(ctx, partial);
            for (int i = 0; i < w * h; ++i) {
                const int x = i % w, y = i / w;
                const bool inside = x >= 1 && x < 5 && y >= 2;
                const float m = inside ? chosen[i] / 255.0f : 0;
                const float mixed =
                    before[i] / 255.0f + (after[i] / 255.0f - before[i] / 255.0f) * m;
                test::check(std::abs(int(actual[i]) - int(quantize(mixed))) <= 1,
                            std::string(name) + " selection differs at " + std::to_string(i));
            }
            // An empty region leaves the destination untouched.
            geometry::upload(ctx, partial, before);
            apply(cmd, partial, nullptr, Rect{2, 2, 0, 3});
            ctx.submit_and_wait(cmd);
            test::check(geometry::download(ctx, partial) == before, "empty region wrote");
        }
    });

    test::run("mask geometry validates atomically", [] {
        auto ctx = Context::create();
        auto source = ctx.create_mask({4, 3});
        auto destination = ctx.create_mask({4, 3});
        auto other = ctx.create_mask({2, 2});
        auto image = ctx.create_image({4, 3});
        auto cmd = ctx.create_commands(1);
        test::error(ErrorCode::invalid_argument, "transform", "destination",
                    [&] { cmd.transform(source, source, TransformOptions{}); });
        test::error(ErrorCode::invalid_argument, "transform", "mask",
                    [&] { cmd.transform(source, destination, TransformOptions{.mask = &destination}); });
        test::error(ErrorCode::invalid_argument, "transform", "mask",
                    [&] { cmd.transform(source, destination, TransformOptions{.mask = &other}); });
        test::error(ErrorCode::invalid_argument, "transform", "matrix",
                    [&] { cmd.transform(source, destination, {.matrix = Affine::scale(0)}); });
        test::error(ErrorCode::invalid_argument, "transform", "filter", [&] {
            cmd.transform(source, destination, TransformOptions{.filter = static_cast<ResizeFilter>(7)});
        });
        test::error(ErrorCode::invalid_argument, "offset", "edge",
                    [&] { cmd.offset(source, destination, {.edge = static_cast<EdgeMode>(7)}); });
        test::error(ErrorCode::invalid_argument, "perspective", "corners",
                    [&] { cmd.perspective(source, destination, {}); });
        test::error(ErrorCode::invalid_argument, "flip", "destination",
                    [&] { cmd.flip(source, other); });
        test::error(ErrorCode::invalid_argument, "flip", "direction", [&] {
            cmd.flip(source, destination, {.direction = static_cast<FlipDirection>(3)});
        });
        test::error(ErrorCode::invalid_argument, "rotate", "degrees", [&] {
            cmd.rotate(source, destination, {.degrees = std::numeric_limits<float>::infinity()});
        });
        test::error(ErrorCode::invalid_argument, "crop", "region", [&] {
            cmd.crop(source, destination, {.origin = {0, 0}, .region = Rect{0, 0, 1, -1}});
        });
        test::error(ErrorCode::invalid_argument, "resize", "filter", [&] {
            cmd.resize(source, destination, {.filter = static_cast<ResizeFilter>(5)});
        });
        test::error(ErrorCode::invalid_resource, "resize", "source",
                    [&] { cmd.resize(Mask{}, destination); });
        (void)image;
        cmd.resize(source, destination);
        ctx.submit_and_wait(cmd);
    });
    return test::finish();
}
