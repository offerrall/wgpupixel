#include "../test.h"
#include <limits>
#include <numbers>

using namespace wgpupixel;

// Shared value types are constexpr-friendly aggregates.
static_assert(Affine{} == Affine::identity());
static_assert(Affine::identity().map({3, -2}) == Point{3, -2});
static_assert(Affine::translate(4, 5).map({1, 2}) == Point{5, 7});
static_assert(Affine::scale(2, -3).map({1, 2}) == Point{2, -6});
static_assert(Affine::scale(0.5f).map({4, 8}) == Point{2, 4});
// Positive degrees turn +x toward +y: clockwise on screen. Right angles are exact.
static_assert(Affine::rotate(90).map({1, 0}) == Point{0, 1});
static_assert(Affine::rotate(-90).map({1, 0}) == Point{0, -1});
static_assert(Affine::rotate(180).map({1, 2}) == Point{-1, -2});
static_assert(Affine::rotate(450) == Affine::rotate(90));
static_assert(Affine::rotate(-720) == Affine::identity());
// The power-of-two factor is exact in float: these remain whole turns.
static_assert(Affine::rotate(360.0f * 0x1p100f) == Affine::identity());
static_assert(Affine::rotate(-360.0f * 0x1p100f) == Affine::identity());
static_assert(Affine::rotate(90, {10, 10}).map({10, 10}) == Point{10, 10});
static_assert(Affine::rotate(90, {10, 10}).map({11, 10}) == Point{10, 11});
// The right operand applies first.
static_assert((Affine::translate(10, 0) * Affine::rotate(90)).map({1, 0}) == Point{10, 1});
static_assert((Affine::rotate(90) * Affine::translate(10, 0)).map({1, 0}) == Point{0, 11});
static_assert(inverse(Affine::translate(3, -4)) == Affine::translate(-3, 4));
static_assert(inverse(Affine::scale(2, 4)) == Affine::scale(0.5f, 0.25f));
static_assert(!inverse(Affine::scale(0, 1)));
static_assert(!inverse(Affine{1, 2, 2, 4, 0, 0}));
static_assert(inverse(Homography{{0x1p500, 0, 0, 0, 0x1p500, 0, 0, 0, 0x1p500}})->m[0] ==
              0x1p-500);
static_assert(inverse(Homography{{0x1p-500, 0, 0, 0, 0x1p-500, 0, 0, 0, 0x1p-500}})->m[0] ==
              0x1p500);
static_assert(!inverse(Homography{{0, 0, 0, 0, 0, 0, 0, 0, 0}}));
// Its determinant is (a-b)*s*s: nonzero because a and b are adjacent doubles.
// All original cofactor/determinant operations fit, but dividing by s first rounds
// a/s and b/s equal and would incorrectly classify this matrix as singular.
constexpr double adjacent_a = 0x1.845235194d32ep+335;
constexpr double adjacent_b = 0x1.845235194d32fp+335;
constexpr double adjacent_s = 0x1.7f97c0b19afc2p+337;
constexpr Homography adjacent_matrix{{adjacent_a, adjacent_b, 0,
                                      adjacent_s, adjacent_s, 0, 0, 0, adjacent_s}};
constexpr auto adjacent_inverse = inverse(adjacent_matrix);
static_assert(adjacent_a != adjacent_b && adjacent_inverse.has_value());
constexpr auto scaled_adjacent_inverses = [] {
    std::array<Homography, 5> result{};
    constexpr std::array factors{1.0, 4.0, 8.0, 16.0, 32.0};
    for (std::size_t i = 0; i < factors.size(); ++i) {
        auto matrix = adjacent_matrix;
        for (auto& value : matrix.m) value *= factors[i];
        // Dereferencing also requires every scaled matrix to be invertible at compile time.
        result[i] = *inverse(matrix);
    }
    return result;
}();
// All original products fit: d*b = 1.25*2^1023 and det = d^3. Normalizing by
// the largest entry first would underflow the determinant and lose this inverse.
constexpr double unequal_d = 0x1.4p332, unequal_b = 0x1p691;
constexpr Homography unequal_matrix{{unequal_d, unequal_b, 0,
                                     0, unequal_d, 0, 0, 0, unequal_d}};
constexpr auto unequal_inverse = inverse(unequal_matrix);
static_assert(unequal_inverse.has_value());
static_assert(!inverse(Homography{{std::numeric_limits<double>::denorm_min(), 0, 0,
                                  0, 1, 0, 0, 0, 1}}));
static_assert(GradientStop{0.5f, {1, 0, 0, 1}}.color.r == 1);
static_assert(std::to_underlying(EdgeMode::transparent) == 0 &&
              std::to_underlying(EdgeMode::mirror) == 3);

int main() {
    test::run("rotation matches the standard library at arbitrary angles", [] {
        for (float degrees = -1080; degrees <= 1080; degrees += 7.25f) {
            const auto m = Affine::rotate(degrees);
            const double radians = double(degrees) * std::numbers::pi / 180;
            test::near(m.a, float(std::cos(radians)), 1e-6f);
            test::near(m.b, float(std::sin(radians)), 1e-6f);
            test::near(m.c, float(-std::sin(radians)), 1e-6f);
            test::near(m.d, float(std::cos(radians)), 1e-6f);
            test::check(m.e == 0 && m.f == 0, "rotation about the origin has no translation");
        }
        const auto nan = Affine::rotate(std::numeric_limits<float>::infinity());
        test::check(std::isnan(nan.a) && !inverse(nan), "non-finite angles must not invert");
    });

    test::run("large finite rotations preserve angles and lengths", [] {
        for (const float degrees : {1e20f, -1e20f, 0x1p100f, -0x1p100f,
                                    std::numeric_limits<float>::max(),
                                    -std::numeric_limits<float>::max()}) {
            // libc reduction is independent of the constexpr binary division.
            const double radians = std::remainder(double(degrees), 360.0) *
                                   std::numbers::pi / 180;
            const auto rotation = Affine::rotate(degrees);
            test::near(rotation.a, float(std::cos(radians)), 1e-6f);
            test::near(rotation.b, float(std::sin(radians)), 1e-6f);
            const auto p = rotation.map({3, 4});
            test::near(std::hypot(p.x, p.y), 5, 1e-6f);
            test::check(inverse(rotation).has_value(), "finite rotation must be invertible");
        }
    });

    test::run("inverse undoes composed transforms", [] {
        const auto transform = Affine::translate(120.5f, -7) * Affine::rotate(33, {64, 32}) *
                               Affine::scale(1.75f, 0.5f) * Affine{1, 0.2f, -0.3f, 1, 0, 0};
        const auto inverted = inverse(transform);
        test::check(inverted.has_value(), "regular transform must invert");
        for (const Point p : {Point{0, 0}, Point{0.5f, 0.5f}, Point{-40, 300}, Point{1000, -3}}) {
            const auto back = inverted->map(transform.map(p));
            test::near(back.x, p.x, 1e-3f);
            test::near(back.y, p.y, 1e-3f);
        }
        const auto identity = *inverted * transform;
        for (const auto [actual, expected] :
             {std::pair{identity.a, 1.0f}, {identity.b, 0.0f}, {identity.c, 0.0f},
              {identity.d, 1.0f}, {identity.e, 0.0f}, {identity.f, 0.0f}}) {
            test::near(actual, expected, 1e-4f);
        }
        const float huge = std::numeric_limits<float>::max();
        test::check(!inverse(Affine::scale(1 / huge, 1 / huge)),
                    "inverse outside float range must be empty");
        test::check(!inverse(Affine{std::numeric_limits<float>::quiet_NaN(), 0, 0, 1, 0, 0}),
                    "NaN transform must not invert");
    });

    test::run("homography inverse retains the scale of the mathematical inverse", [] {
        // x'=2x+4, y'=4y-8: solve independently as x=x'/2-2, y=y'/4+2.
        constexpr Homography forward{{2, 0, 4, 0, 4, -8, 0, 0, 1}};
        constexpr Homography expected{{0.5, 0, -2, 0, 0.25, 2, 0, 0, 1}};
        for (const double scalar : {1.0, -1.0, 1e110, 1e-110, -1e110, -1e-110,
                                     1e200, 1e-200, 1e300, 1e-300}) {
            auto matrix = forward;
            for (auto& value : matrix.m) value *= scalar;
            const auto inverted = inverse(matrix);
            test::check(inverted.has_value(), "globally scaled invertible matrix was rejected");
            for (int i = 0; i < 9; ++i) {
                test::check(std::isfinite(inverted->m[i]) &&
                                std::abs(inverted->m[i] * scalar - expected.m[i]) < 1e-12,
                            "inverse changed its homogeneous scale or coefficient");
            }
        }
    });

    test::run("homography normalization preserves large translations and unequal axes", [] {
        // Force runtime evaluation as well as the constant expression checked above.
        constexpr std::array factors{1.0, 4.0, 8.0, 16.0, 32.0};
        for (std::size_t i = 0; i < factors.size(); ++i) {
            volatile double adjacent_runtime_a = adjacent_a * factors[i];
            auto adjacent_runtime = adjacent_matrix;
            for (auto& value : adjacent_runtime.m) value *= factors[i];
            adjacent_runtime.m[0] = adjacent_runtime_a;
            const auto adjacent_result = inverse(adjacent_runtime);
            test::check(adjacent_result && adjacent_result->m == scaled_adjacent_inverses[i].m,
                        "constexpr normalization changed an already representable inverse");
        }
        volatile double unequal_runtime_b = unequal_b;
        auto unequal_runtime = unequal_matrix;
        unequal_runtime.m[1] = unequal_runtime_b;
        const auto unequal_result = inverse(unequal_runtime);
        test::check(unequal_result && unequal_result->m == unequal_inverse->m,
                    "constexpr inversion lost a representable inverse with unequal coefficients");
        // Triangular inverse, solved independently: diagonal 1/d, off-diagonal -b/d^2.
        test::check(std::abs(unequal_inverse->m[0] * unequal_d - 1) < 1e-12 &&
                        std::abs(unequal_inverse->m[1] / ((-unequal_b / unequal_d) / unequal_d) - 1) <
                            1e-12,
                    "unequal triangular inverse has incorrect coefficients");
        for (const double scalar : {1e200, 1e-200, 1e300, 1e-300}) {
            const Homography matrix{{scalar, 0, 0, 0, 1 / scalar, 0, 0, 0, 1}};
            const auto inverted = inverse(matrix);
            test::check(inverted.has_value(), "unequal finite axes must remain invertible");
            test::check(std::abs(inverted->m[0] * scalar - 1) < 1e-12 &&
                            std::abs(inverted->m[4] / scalar - 1) < 1e-12 && inverted->m[8] == 1,
                        "unequal axes inverse has incorrect coefficients");
        }
        const Homography translated{{1, 0, 1e300, 0, 1, -1e300, 0, 0, 1}};
        const auto inverted = inverse(translated);
        test::check(inverted && inverted->m == Homography{{1, 0, -1e300, 0, 1, 1e300, 0, 0, 1}}.m,
                    "large translation must invert without normalization losing its determinant");
        test::check(!inverse(Homography{{1e200, 0, 0, 0, 0, 0, 0, 0, 1e200}}),
                    "large singular matrix must stay singular");
        test::check(!inverse(Homography{{std::numeric_limits<double>::infinity(), 0, 0,
                                       0, 1, 0, 0, 0, 1}}),
                    "non-finite homography must not invert");
        test::check(!inverse(Homography{{std::numeric_limits<double>::denorm_min(), 0, 0,
                                       0, 1, 0, 0, 0, 1}}),
                    "unrepresentable inverse must be empty");
    });

    test::run("public value types are assignable aggregates", [] {
        Affine transform{};
        transform = Affine::translate(1, 2);
        transform.e = 5;
        Point point{1, 1};
        point = transform.map(point);
        test::check(point == Point{6, 3}, "assigned transform must apply");
        std::array stops{GradientStop{0, {0, 0, 0, 1}}, GradientStop{1, {1, 1, 1, 1}}};
        stops[0].position = 0.25f;
        test::check(stops[0].position == 0.25f, "stops must be assignable");
    });
    return test::finish();
}
