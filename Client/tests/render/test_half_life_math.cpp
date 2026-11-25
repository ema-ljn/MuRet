#include "Math/HalfLifeMath.h"

#include "Math/RoundedMath.h"

#include "support/FloatBits.h"
#include "support/Fnv1a.h"

#include <gtest/gtest.h>

#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <random>
#include <span>
#include <vector>

// Client::Render::Models: la mathlib de Half-Life que usa la pose del original (0x5fcd20, 0x5fce90,
// 0x5fc700, 0x5fcdb0, 0x5fc8f0, 0x5fca90, 0x5fccc0 y 0x5fcbd0).
namespace
{
using Client::Render::Models::AngleMatrix;
using Client::Render::Models::AngleQuaternion;
using Client::Render::Models::BoneTransform;
using Client::Render::Models::ConcatTransforms;
using Client::Render::Models::QuaternionMatrix;
using Client::Render::Models::QuaternionsAlmostEqual;
using Client::Render::Models::QuaternionSlerp;
using Client::Render::Models::RoundedCos;
using Client::Render::Models::RoundedSin;
using Client::Render::Models::VectorRotate;
using Client::Render::Models::VectorTransform;
using Client::Tests::ExponentRange;
using Client::Tests::FloatFromBits;
using Client::Tests::Fnv1a64;
using Client::Tests::WithoutSign;

// Lo que separa una cuenta en float de la del original en la x87 (la CL6.0 lo midió: 5,4e-7 o
// menos). Un error de verdad (un signo, un eje, la traslación en otro lado) mueve mucho más.
constexpr float Tolerance = 1e-5f;

void ExpectNear(const glm::vec3& actual, const glm::vec3& expected)
{
    EXPECT_NEAR(actual.x, expected.x, Tolerance);
    EXPECT_NEAR(actual.y, expected.y, Tolerance);
    EXPECT_NEAR(actual.z, expected.z, Tolerance);
}

void ExpectNear(const glm::quat& actual, const glm::quat& expected)
{
    EXPECT_NEAR(actual.x, expected.x, Tolerance);
    EXPECT_NEAR(actual.y, expected.y, Tolerance);
    EXPECT_NEAR(actual.z, expected.z, Tolerance);
    EXPECT_NEAR(actual.w, expected.w, Tolerance);
}

// Un cuarto de vuelta alrededor de z: (x, y, z) = (0, 0, sin 45°) y w = cos 45°, en ese orden de
// memoria. Con glm::quat(x, y, z, w) en vez de wxyz es otro giro: el personaje sale retorcido.
TEST(HalfLifeMathTest, AQuarterTurnAroundZ)
{
    const glm::quat turn = AngleQuaternion({0.0f, 0.0f, std::numbers::pi_v<float> / 2});

    const float half = std::sqrt(0.5f);
    ExpectNear(turn, glm::quat::wxyz(half, 0.0f, 0.0f, half));
    ExpectNear(VectorRotate({1.0f, 0.0f, 0.0f}, QuaternionMatrix(turn)), {0.0f, 1.0f, 0.0f});
}

// 0x5fcd20, como lo escribe Half-Life: el medio ángulo de cada eje, y Rz · Ry · Rx.
TEST(HalfLifeMathTest, AngleQuaternionIsTheHalfLifeFormula)
{
    for (const glm::vec3 angles : {glm::vec3{0.3f, -1.2f, 2.5f}, glm::vec3{-2.9f, 0.7f, -0.4f},
                                   glm::vec3{1.5f, 1.5f, 1.5f}, glm::vec3{0.0f, 3.1f, -3.1f}})
    {
        const float sr = RoundedSin(angles.x / 2);
        const float cr = RoundedCos(angles.x / 2);
        const float sp = RoundedSin(angles.y / 2);
        const float cp = RoundedCos(angles.y / 2);
        const float sy = RoundedSin(angles.z / 2);
        const float cy = RoundedCos(angles.z / 2);
        const glm::quat expected = glm::quat::wxyz(cr * cp * cy + sr * sp * sy, sr * cp * cy - cr * sp * sy,
                                                   cr * sp * cy + sr * cp * sy, cr * cp * sy - sr * sp * cy);

        ExpectNear(AngleQuaternion(angles), expected);
    }
}

// 0x5fc700: cada componente a 0,001 o menos. Una sola que pasa ya no es "igual".
TEST(HalfLifeMathTest, AlmostEqualIsAThousandthPerComponent)
{
    const glm::quat p = glm::quat::wxyz(0.9f, 0.1f, 0.2f, 0.3f);

    EXPECT_TRUE(QuaternionsAlmostEqual(p, p));
    EXPECT_TRUE(QuaternionsAlmostEqual(p, glm::quat::wxyz(0.9009f, 0.0991f, 0.2009f, 0.2991f)));
    EXPECT_FALSE(QuaternionsAlmostEqual(p, glm::quat::wxyz(0.9f, 0.1f, 0.2f, 0.3011f)));
    EXPECT_FALSE(QuaternionsAlmostEqual(p, glm::quat::wxyz(0.8989f, 0.1f, 0.2f, 0.3f)));
}

// Con el producto negativo, Half-Life da vuelta q y va hacia −q: con t = 1 llega a −q, el mismo giro.
// Sin dar vuelta, el camino sería el largo.
TEST(HalfLifeMathTest, SlerpFlipsLikeHalfLife)
{
    const glm::quat p = AngleQuaternion({0.0f, 0.0f, 0.2f});
    const glm::quat q = -AngleQuaternion({0.0f, 0.0f, 0.6f});

    ExpectNear(QuaternionSlerp(p, q, 1.0f), -q);
    ExpectNear(QuaternionSlerp(p, q, 0.0f), p);
    ExpectNear(QuaternionSlerp(p, q, 0.5f), AngleQuaternion({0.0f, 0.0f, 0.4f}));
}

// Con los dos casi iguales (el cos en float da 1), los pesos son 1 − t y t, sin senos.
TEST(HalfLifeMathTest, SlerpIsLinearBelowOneInAHundredMillion)
{
    const glm::quat p = glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::quat q = glm::quat::wxyz(1.0f, 0.0f, 0.0f, 1e-5f);

    const glm::quat middle = QuaternionSlerp(p, q, 0.25f);

    EXPECT_EQ(middle.w, 1.0f);
    EXPECT_FLOAT_EQ(middle.z, 0.25f * 1e-5f);
}

// El giro va en las tres primeras columnas y la traslación en m[3]: VectorTransform la suma.
TEST(HalfLifeMathTest, TheMatrixIsColumnMajor)
{
    BoneTransform m = QuaternionMatrix(AngleQuaternion({0.0f, 0.0f, std::numbers::pi_v<float> / 2}));
    EXPECT_EQ(m[3], glm::vec3(0.0f));
    m[3] = glm::vec3(10.0f, 20.0f, 30.0f);

    ExpectNear(VectorTransform({1.0f, 0.0f, 0.0f}, m), {10.0f, 21.0f, 30.0f});
    // Una fila de la 3 × 4 de Half-Life es una fila de la matriz, no una columna de glm: m[0][1] es
    // la fila 1 de la columna 0, el seno del giro.
    EXPECT_NEAR(m[0][1], 1.0f, Tolerance);
    EXPECT_NEAR(m[1][0], -1.0f, Tolerance);
}

// 0x5fc8f0, en grados: 90 alrededor de z lleva x a y.
TEST(HalfLifeMathTest, AngleMatrixIsInDegrees)
{
    const BoneTransform m = AngleMatrix({0.0f, 0.0f, 90.0f});

    ExpectNear(VectorRotate({1.0f, 0.0f, 0.0f}, m), {0.0f, 1.0f, 0.0f});
    EXPECT_EQ(m[3], glm::vec3(0.0f));
}

// 0x5fc8f0 como lo escribe Half-Life, con el π/180 en float, contra AngleMatrix.
TEST(HalfLifeMathTest, AngleMatrixIsTheHalfLifeFormula)
{
    constexpr float DegreesToRadians = 0.017453292f;
    for (const glm::vec3 degrees :
         {glm::vec3{30.0f, -70.0f, 100.0f}, glm::vec3{-170.0f, 45.0f, 12.5f}, glm::vec3{90.0f, 90.0f, 90.0f}})
    {
        const float sr = RoundedSin(degrees.x * DegreesToRadians);
        const float cr = RoundedCos(degrees.x * DegreesToRadians);
        const float sp = RoundedSin(degrees.y * DegreesToRadians);
        const float cp = RoundedCos(degrees.y * DegreesToRadians);
        const float sy = RoundedSin(degrees.z * DegreesToRadians);
        const float cy = RoundedCos(degrees.z * DegreesToRadians);
        // matrix[fila][columna] de Half-Life.
        const std::array<std::array<float, 3>, 3> matrix = {{
            {cp * cy, sr * sp * cy - cr * sy, sr * sy + cr * sp * cy},
            {cp * sy, cr * cy + sr * sp * sy, cr * sp * sy - sr * cy},
            {-sp, sr * cp, cr * cp},
        }};

        const BoneTransform m = AngleMatrix(degrees);

        for (std::size_t row = 0; row < 3; ++row)
        {
            for (std::size_t column = 0; column < 3; ++column)
            {
                EXPECT_NEAR(m[static_cast<int>(column)][static_cast<int>(row)], matrix[row][column], Tolerance)
                    << "fila " << row << ", columna " << column;
            }
        }
    }
}

// 0x5fca90: el hijo primero y el padre después, como llevar un punto del hueso al mundo.
TEST(HalfLifeMathTest, ConcatIsParentTimesChild)
{
    BoneTransform parent = AngleMatrix({0.0f, 0.0f, 90.0f});
    parent[3] = glm::vec3(100.0f, 0.0f, 0.0f);
    BoneTransform child = AngleMatrix({90.0f, 0.0f, 0.0f});
    child[3] = glm::vec3(0.0f, 5.0f, 0.0f);
    const glm::vec3 point{1.0f, 2.0f, 3.0f};

    const BoneTransform both = ConcatTransforms(parent, child);

    ExpectNear(VectorTransform(point, both), VectorTransform(VectorTransform(point, child), parent));
}

// 0x5fcbd0 no suma la traslación: una normal no se mueve con el hueso.
TEST(HalfLifeMathTest, VectorRotateDoesNotMove)
{
    BoneTransform m = AngleMatrix({0.0f, 0.0f, 90.0f});
    m[3] = glm::vec3(100.0f, 200.0f, 300.0f);

    ExpectNear(VectorRotate({1.0f, 0.0f, 0.0f}, m), {0.0f, 1.0f, 0.0f});
}

// The hash is the one Linux gives: any other system has to give it too.
TEST(HalfLifeMathTest, ThePoseGivesTheSameBitsOnEverySystem)
{
    constexpr ExponentRange Radians{-8, 1};
    constexpr ExponentRange Degrees{-2, 8};
    constexpr ExponentRange Weights{-8, -1};
    std::mt19937_64 engine(20261121);
    std::vector<float> values;
    for (std::size_t i = 0; i < 20000; ++i)
    {
        glm::vec3 from;
        from.x = FloatFromBits(engine, Radians);
        from.y = FloatFromBits(engine, Radians);
        from.z = FloatFromBits(engine, Radians);
        glm::vec3 to;
        to.x = FloatFromBits(engine, Radians);
        to.y = FloatFromBits(engine, Radians);
        to.z = FloatFromBits(engine, Radians);
        glm::vec3 degrees;
        degrees.x = FloatFromBits(engine, Degrees);
        degrees.y = FloatFromBits(engine, Degrees);
        degrees.z = FloatFromBits(engine, Degrees);
        const float weight = WithoutSign(FloatFromBits(engine, Weights));

        const glm::quat p = AngleQuaternion(from);
        const glm::quat between = QuaternionSlerp(p, AngleQuaternion(to), weight);
        const BoneTransform turn = AngleMatrix(degrees);
        const glm::vec3 moved = VectorTransform(from, ConcatTransforms(QuaternionMatrix(between), turn));

        values.insert(values.end(), {p.x, p.y, p.z, p.w, between.x, between.y, between.z, between.w});
        for (int column = 0; column < 4; ++column)
        {
            values.insert(values.end(), {turn[column].x, turn[column].y, turn[column].z});
        }
        values.insert(values.end(), {moved.x, moved.y, moved.z});
    }

    EXPECT_EQ(Fnv1a64(std::as_bytes(std::span(values))), 0x77ae730862537224u);
}
} // namespace
