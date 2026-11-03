#include "Quaternion.h"
#include "Slerp.h"

#include <glm/ext/quaternion_common.hpp>
#include <glm/ext/quaternion_float.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

// Labs::HalfLifeSlerp, Labs::SlerpBranch y Labs::AlmostEqual (CL6.0, paso 3): el slerp de
// 0x5fce90 y la igualdad de 0x5fc700, contra glm.
namespace
{
using Labs::AlmostEqual;
using Labs::AngleQuaternion;
using Labs::HalfLifeSlerp;
using Labs::Quat;
using Labs::SlerpBranch;
using Labs::SlerpBranchForCosine;
using Labs::SlerpCase;
using Labs::Vec3;

constexpr float Pi = std::numbers::pi_v<float>;
constexpr float Tolerance = 1e-6f;
constexpr Quat Identity{};

glm::quat ToGlm(Quat q)
{
    return glm::quat::wxyz(q.w, q.x, q.y, q.z);
}

Quat Negated(Quat q)
{
    return Quat{.x = -q.x, .y = -q.y, .z = -q.z, .w = -q.w};
}

double Dot(Quat p, Quat q)
{
    return static_cast<double>(p.x) * q.x + static_cast<double>(p.y) * q.y + static_cast<double>(p.z) * q.z +
           static_cast<double>(p.w) * q.w;
}

void ExpectNear(Quat actual, Quat expected, float tolerance)
{
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
    EXPECT_NEAR(actual.w, expected.w, tolerance);
}

// Pares de giros sin azar (las distribuciones de <random> dan otros números en cada biblioteca
// estándar): los ángulos de −π a π cada π/5, y cada uno con el que está 37 lugares más adelante.
std::vector<std::pair<Quat, Quat>> RotationPairs()
{
    std::vector<Quat> rotations;
    for (int i = -5; i <= 5; ++i)
    {
        for (int j = -5; j <= 5; ++j)
        {
            for (int k = -5; k <= 5; ++k)
            {
                rotations.push_back(AngleQuaternion(Vec3{static_cast<float>(i) * Pi / 5, static_cast<float>(j) * Pi / 5,
                                                         static_cast<float>(k) * Pi / 5}));
            }
        }
    }
    std::vector<std::pair<Quat, Quat>> pairs;
    for (std::size_t i = 0; i < rotations.size(); ++i)
    {
        pairs.emplace_back(rotations[i], rotations[(i + 37) % rotations.size()]);
    }
    return pairs;
}

// A mitad de camino entre 0 y un cuarto de vuelta alrededor de z está el octavo de vuelta.
TEST(SlerpTest, HalfWayIsTheMiddleRotation)
{
    const Quat quarter = AngleQuaternion(Vec3{0, 0, Pi / 2});
    ExpectNear(HalfLifeSlerp(Identity, quarter, 0.5f), AngleQuaternion(Vec3{0, 0, Pi / 4}), Tolerance);
    ExpectNear(HalfLifeSlerp(Identity, quarter, 0), Identity, Tolerance);
    ExpectNear(HalfLifeSlerp(Identity, quarter, 1), quarter, Tolerance);
}

// Σ(p + q)² − Σ(p − q)² = 4 · p·q para cualquier par: dar vuelta q cuando Σ(p − q)² > Σ(p + q)² es
// darla cuando el producto es negativo, lo mismo que glm. Lejos de un producto de 0, los dos dan el
// mismo cuaternión; con 0 justo, ninguno da vuelta.
TEST(SlerpTest, TheFlipIsGlmsForUnitQuaternions)
{
    int compared = 0;
    for (const auto& [p, q] : RotationPairs())
    {
        if (std::fabs(Dot(p, q)) < 1e-3)
        {
            continue;
        }
        ++compared;
        for (const float t : {0.25f, 0.5f, 0.737f})
        {
            const glm::quat fromGlm = glm::slerp(ToGlm(p), ToGlm(q), t);
            ExpectNear(HalfLifeSlerp(p, q, t), Quat{.x = fromGlm.x, .y = fromGlm.y, .z = fromGlm.z, .w = fromGlm.w},
                       1e-5f);
        }
    }
    EXPECT_GT(compared, 1000);

    // Media vuelta alrededor de x desde 0: el producto es 0, y los dos van por el mismo lado.
    const Quat halfTurn{.x = 1, .y = 0, .z = 0, .w = 0};
    const glm::quat fromGlm = glm::slerp(ToGlm(Identity), ToGlm(halfTurn), 0.5f);
    const Quat byHand = HalfLifeSlerp(Identity, halfTurn, 0.5f);
    EXPECT_NEAR(byHand.x, fromGlm.x, Tolerance);
    EXPECT_NEAR(byHand.w, fromGlm.w, Tolerance);
    EXPECT_NEAR(byHand.x, std::sqrt(0.5f), Tolerance);
}

// 1 − cos ≤ 1e-8 en double, pero con el cos ya en float: el float más cercano a 1 por debajo es
// 1 − 2⁻²⁴, unos 6e-8, que ya pasa el 1e-8. El original va por los senos salvo con el cos en 1 o
// más; glm, en cambio, pasa a lineal desde cos > 1 − 2⁻²³ (su epsilon<float>()).
TEST(SlerpTest, GoesLinearOnlyBelowOneInAHundredMillion)
{
    const float cosine = std::nextafter(1.0f, 0.0f);
    const float sine = std::sqrt((1 - cosine) * (1 + cosine));
    const Quat almost{.x = 0, .y = 0, .z = sine, .w = cosine};
    EXPECT_EQ(SlerpBranch(Identity, almost), SlerpCase::Sines);
    EXPECT_EQ(SlerpBranch(Identity, Identity), SlerpCase::Linear);

    // Un cos de más de 1, de un cuaternión que no es unitario: 1 − cos es negativo, y va lineal.
    const Quat longer{.x = 0, .y = 0, .z = 0, .w = std::nextafter(1.0f, 2.0f)};
    EXPECT_EQ(SlerpBranch(Identity, longer), SlerpCase::Linear);

    // Por caminos distintos, el mismo cuaternión: el giro es de unos 0,04°.
    const glm::quat fromGlm = glm::slerp(ToGlm(Identity), ToGlm(almost), 0.5f);
    const Quat byHand = HalfLifeSlerp(Identity, almost, 0.5f);
    EXPECT_NEAR(byHand.z, fromGlm.z, 1e-7f);
    EXPECT_NEAR(byHand.w, fromGlm.w, 1e-7f);
}

// Las dos comparaciones miran el cos en momentos distintos: la de los opuestos, el que quedó en la
// pila; la lineal, el que se guardó en float. Un cos de 1 − 5e-9 es 1 en float y va lineal; uno de
// −1 + 2e-8 también es −1 en float, pero la primera comparación lo ve antes de redondear y no lo
// manda a los opuestos.
TEST(SlerpTest, EachComparisonSeesItsOwnCosine)
{
    EXPECT_EQ(static_cast<float>(1 - 5e-9), 1.0f);
    EXPECT_EQ(SlerpBranchForCosine(1 - 5e-9), SlerpCase::Linear);
    EXPECT_EQ(SlerpBranchForCosine(1 - 0x1p-24), SlerpCase::Sines);
    EXPECT_EQ(SlerpBranchForCosine(1.0), SlerpCase::Linear);

    EXPECT_EQ(static_cast<float>(-1 + 2e-8), -1.0f);
    EXPECT_EQ(SlerpBranchForCosine(-1 + 2e-8), SlerpCase::Sines);
    EXPECT_EQ(SlerpBranchForCosine(-1 + 5e-9), SlerpCase::Opposite);
    EXPECT_EQ(SlerpBranchForCosine(-1.0), SlerpCase::Opposite);
}

// La rama de los opuestos pide 1 + cos ≤ 1e-8, un cos de casi −1. Después de dar vuelta q, el
// producto es positivo: con números de verdad (sin NaN) no corre nunca, ni con q = −p.
TEST(SlerpTest, ThePerpendicularCaseNeverRunsAfterTheFlip)
{
    for (const auto& [p, q] : RotationPairs())
    {
        EXPECT_NE(SlerpBranch(p, q), SlerpCase::Opposite);
        EXPECT_NE(SlerpBranch(p, Negated(p)), SlerpCase::Opposite);
    }

    // −p se da vuelta a p, y el slerp de p a p es p.
    const Quat p = AngleQuaternion(Vec3{0.3f, -1.2f, 2.5f});
    EXPECT_EQ(SlerpBranch(p, Negated(p)), SlerpCase::Linear);
    ExpectNear(HalfLifeSlerp(p, Negated(p), 0.3f), p, Tolerance);
}

// Lo único que llega a la rama de los opuestos es un NaN: ninguna comparación con NaN es verdadera,
// así que q no se da vuelta y el cos NaN no pasa el 1 + cos > 1e-8 (en la x87, FCOMP con NaN prende
// C0 y C3, y el salto de 0x5fcf24 va a los opuestos). La w del resultado se queda en p.z.
TEST(SlerpTest, OnlyANanReachesTheOppositeBranch)
{
    const Quat broken{.x = std::numeric_limits<float>::quiet_NaN(), .y = 0, .z = 0, .w = 1};
    EXPECT_EQ(SlerpBranch(broken, Identity), SlerpCase::Opposite);
    const Quat result = HalfLifeSlerp(broken, Identity, 0.5f);
    EXPECT_TRUE(std::isnan(result.x));
    EXPECT_TRUE(std::isnan(result.y));
    EXPECT_NEAR(result.z, -std::sqrt(0.5f), Tolerance);
    EXPECT_EQ(result.w, 0);
}

// 0x5fc700: cada componente a 0,001 o menos de la otra.
TEST(SlerpTest, AlmostEqualKeysAreTheSameKey)
{
    const Quat key{.x = 0.6f, .y = 0, .z = 0, .w = 0.8f};
    EXPECT_TRUE(AlmostEqual(key, key));
    EXPECT_TRUE(AlmostEqual(key, Quat{.x = 0.6009f, .y = 0, .z = 0, .w = 0.8f}));
    EXPECT_FALSE(AlmostEqual(key, Quat{.x = 0.6011f, .y = 0, .z = 0, .w = 0.8f}));
    EXPECT_FALSE(AlmostEqual(key, Quat{.x = 0.6f, .y = 0, .z = 0, .w = 0.7988f}));
    // q y −q son el mismo giro, pero no son iguales para 0x5fc700.
    EXPECT_FALSE(AlmostEqual(key, Negated(key)));
}

// El 0,001 es un double y la resta queda en la pila: 0,001f es un poco más que 0,001 (un float no
// lo guarda exacto) y no pasa; el float de abajo, sí.
TEST(SlerpTest, TheToleranceIsTheDoubleOneThousandth)
{
    EXPECT_GT(static_cast<double>(0.001f), 0.001);
    EXPECT_FALSE(AlmostEqual(Identity, Quat{.x = 0.001f, .y = 0, .z = 0, .w = 1}));
    EXPECT_TRUE(AlmostEqual(Identity, Quat{.x = std::nextafter(0.001f, 0.0f), .y = 0, .z = 0, .w = 1}));
}
} // namespace
