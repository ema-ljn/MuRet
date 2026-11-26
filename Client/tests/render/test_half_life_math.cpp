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

// Client::Math: the Half-Life mathlib the original's pose uses (0x5fcd20, 0x5fce90, 0x5fc700, 0x5fcdb0, 0x5fc8f0,
// 0x5fca90, 0x5fccc0 and 0x5fcbd0).
namespace
{
using Client::Math::AngleMatrix;
using Client::Math::AngleQuaternion;
using Client::Math::BoneTransform;
using Client::Math::ConcatTransforms;
using Client::Math::QuaternionMatrix;
using Client::Math::QuaternionsAlmostEqual;
using Client::Math::QuaternionSlerp;
using Client::Math::RoundedCos;
using Client::Math::RoundedSin;
using Client::Math::VectorRotate;
using Client::Math::VectorTransform;
using Client::Tests::ExponentRange;
using Client::Tests::FloatFromBits;
using Client::Tests::Fnv1a64;
using Client::Tests::WithoutSign;

// What separates a float computation from the original's on the x87 (CL6.0 measured it: 5.4e-7 or less). A real error
// (a sign, an axis, the translation elsewhere) moves much more.
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

// A quarter turn about z: (x, y, z) = (0, 0, sin 45°) and w = cos 45°, in that memory order. With glm::quat(x, y, z, w)
// instead of wxyz it is another rotation: the character comes out twisted.
TEST(HalfLifeMathTest, AQuarterTurnAroundZ)
{
    const glm::quat turn = AngleQuaternion({0.0f, 0.0f, std::numbers::pi_v<float> / 2});

    const float half = std::sqrt(0.5f);
    ExpectNear(turn, glm::quat::wxyz(half, 0.0f, 0.0f, half));
    ExpectNear(VectorRotate({1.0f, 0.0f, 0.0f}, QuaternionMatrix(turn)), {0.0f, 1.0f, 0.0f});
}

// 0x5fcd20, as Half-Life writes it: each axis's half angle, and Rz · Ry · Rx.
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

// 0x5fc700: each component within 0.001. A single one beyond that is no longer "equal".
TEST(HalfLifeMathTest, AlmostEqualIsAThousandthPerComponent)
{
    const glm::quat p = glm::quat::wxyz(0.9f, 0.1f, 0.2f, 0.3f);

    EXPECT_TRUE(QuaternionsAlmostEqual(p, p));
    EXPECT_TRUE(QuaternionsAlmostEqual(p, glm::quat::wxyz(0.9009f, 0.0991f, 0.2009f, 0.2991f)));
    EXPECT_FALSE(QuaternionsAlmostEqual(p, glm::quat::wxyz(0.9f, 0.1f, 0.2f, 0.3011f)));
    EXPECT_FALSE(QuaternionsAlmostEqual(p, glm::quat::wxyz(0.8989f, 0.1f, 0.2f, 0.3f)));
}

// With a negative product Half-Life flips q and goes towards −q: with t = 1 it reaches −q, the same rotation. Without
// the flip the path would be the long one.
TEST(HalfLifeMathTest, SlerpFlipsLikeHalfLife)
{
    const glm::quat p = AngleQuaternion({0.0f, 0.0f, 0.2f});
    const glm::quat q = -AngleQuaternion({0.0f, 0.0f, 0.6f});

    ExpectNear(QuaternionSlerp(p, q, 1.0f), -q);
    ExpectNear(QuaternionSlerp(p, q, 0.0f), p);
    ExpectNear(QuaternionSlerp(p, q, 0.5f), AngleQuaternion({0.0f, 0.0f, 0.4f}));
}

// With both almost equal (the cos in float gives 1), the weights are 1 − t and t, without sines.
TEST(HalfLifeMathTest, SlerpIsLinearBelowOneInAHundredMillion)
{
    const glm::quat p = glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::quat q = glm::quat::wxyz(1.0f, 0.0f, 0.0f, 1e-5f);

    const glm::quat middle = QuaternionSlerp(p, q, 0.25f);

    EXPECT_EQ(middle.w, 1.0f);
    EXPECT_FLOAT_EQ(middle.z, 0.25f * 1e-5f);
}

// The rotation goes in the first three columns and the translation in m[3]: VectorTransform adds it.
TEST(HalfLifeMathTest, TheMatrixIsColumnMajor)
{
    BoneTransform m = QuaternionMatrix(AngleQuaternion({0.0f, 0.0f, std::numbers::pi_v<float> / 2}));
    EXPECT_EQ(m[3], glm::vec3(0.0f));
    m[3] = glm::vec3(10.0f, 20.0f, 30.0f);

    ExpectNear(VectorTransform({1.0f, 0.0f, 0.0f}, m), {10.0f, 21.0f, 30.0f});
    // A row of Half-Life's 3 × 4 is a row of the matrix, not a glm column: m[0][1] is row 1 of column 0, the rotation's
    // sine.
    EXPECT_NEAR(m[0][1], 1.0f, Tolerance);
    EXPECT_NEAR(m[1][0], -1.0f, Tolerance);
}

// 0x5fc8f0, in degrees: 90 about z takes x to y.
TEST(HalfLifeMathTest, AngleMatrixIsInDegrees)
{
    const BoneTransform m = AngleMatrix({0.0f, 0.0f, 90.0f});

    ExpectNear(VectorRotate({1.0f, 0.0f, 0.0f}, m), {0.0f, 1.0f, 0.0f});
    EXPECT_EQ(m[3], glm::vec3(0.0f));
}

// 0x5fc8f0 as Half-Life writes it, with π/180 in float, against AngleMatrix.
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
        // Half-Life's matrix[row][column].
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

// 0x5fca90: the child first and the parent after, like taking a point from the bone to the world.
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

// 0x5fcbd0 does not add the translation: a normal does not move with the bone.
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
