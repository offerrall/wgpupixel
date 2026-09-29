#include "../test.h"
#include <limits>

using namespace wgpupixel;

namespace {
std::uint32_t hash(std::uint32_t value) {
    value = (value ^ (value >> 16)) * 0x7feb352du;
    value = (value ^ (value >> 15)) * 0x846ca68bu;
    return value ^ (value >> 16);
}
float sample(std::uint32_t key) {
    return float(hash(key) >> 8) / 16777216.0f;
}
} // namespace

int main() {
    test::run("noise seeded CPU reference, channels and alpha", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({7, 5});
        for (std::uint32_t seed : {0u, 12345u, std::numeric_limits<std::uint32_t>::max()}) {
            for (bool monochrome : {true, false}) {
                auto commands = ctx.create_commands(2);
                commands.fill(image, {.color = {0, 0, 0, 0}});
                commands.noise(image, {.seed = seed, .monochrome = monochrome});
                ctx.submit_and_wait(commands);
                std::vector<float> expected;
                for (std::uint32_t y = 0; y < 5; ++y) {
                    for (std::uint32_t x = 0; x < 7; ++x) {
                        const auto key = x * 0x1f123bb5u ^ y * 0x5f356495u ^ seed;
                        const float r = sample(key);
                        expected.insert(expected.end(),
                                        {r, monochrome ? r : sample(key + 0x9e3779b9u),
                                         monochrome ? r : sample(key + 0x3c6ef372u), 1});
                    }
                }
                const auto encoded = test::encode(expected);
                const auto actual = test::read(ctx, image);
                for (std::size_t i = 0; i < encoded.size(); ++i) {
                    test::check(std::abs(int(actual[i]) - int(encoded[i])) <= 1,
                                "noise differs from deterministic CPU hash");
                }
            }
        }
    });
    test::run("noise repeatability and seed changes", [] {
        auto ctx = Context::create();
        auto image = ctx.create_image({9, 3});
        auto render = [&](std::uint32_t seed) {
            auto commands = ctx.create_commands(1);
            commands.noise(image, {.seed = seed, .monochrome = false});
            ctx.submit_and_wait(commands);
            return test::read(ctx, image);
        };
        const auto first = render(42);
        test::check(first == render(42), "same seed changed output");
        test::check(first != render(43), "different seed did not change output");
        bool different_channels = false;
        for (std::size_t i = 0; i < first.size(); i += 4) {
            different_channels |= first[i] != first[i + 1] || first[i] != first[i + 2];
            test::check(first[i + 3] == 255, "noise did not overwrite alpha with one");
        }
        test::check(different_channels, "color noise has identical channels");
    });
    return test::finish();
}
