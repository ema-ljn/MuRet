#include "Render/Models/RoundedTransforms.h"

#include "Render/Models/RoundedMath.h"

#include "support/FloatBits.h"
#include "support/Fnv1a.h"

#include <gtest/gtest.h>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <random>
#include <span>
#include <vector>

namespace
{
using Client::Render::Models::RoundedCos;
using Client::Render::Models::RoundedPerspective;
using Client::Render::Models::RoundedRotate;
using Client::Render::Models::RoundedSin;
using Client::Render::Models::RoundedTan;
using Client::Tests::ExponentRange;
using Client::Tests::FloatFromBits;
using Client::Tests::Fnv1a64;
using Client::Tests::WithoutSign;

constexpr float Angle = 0.7f;

void Append(std::vector<float>& values, const glm::mat4& matrix)
{
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            values.push_back(matrix[column][row]);
        }
    }
}

// glm keeps columns: each line below is one.
TEST(RoundedTransformsTest, TurnsAroundEachAxisLikeGlRotatef)
{
    const float c = RoundedCos(Angle);
    const float s = RoundedSin(Angle);
    const glm::mat4 identity(1.0f);

    EXPECT_EQ(RoundedRotate(identity, Angle, {1.0f, 0.0f, 0.0f}), glm::mat4(1.0f, 0.0f, 0.0f, 0.0f, //
                                                                            0.0f, c, s, 0.0f,       //
                                                                            0.0f, -s, c, 0.0f,      //
                                                                            0.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(RoundedRotate(identity, Angle, {0.0f, 1.0f, 0.0f}), glm::mat4(c, 0.0f, -s, 0.0f,      //
                                                                            0.0f, 1.0f, 0.0f, 0.0f, //
                                                                            s, 0.0f, c, 0.0f,       //
                                                                            0.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(RoundedRotate(identity, Angle, {0.0f, 0.0f, 1.0f}), glm::mat4(c, s, 0.0f, 0.0f,       //
                                                                            -s, c, 0.0f, 0.0f,      //
                                                                            0.0f, 0.0f, 1.0f, 0.0f, //
                                                                            0.0f, 0.0f, 0.0f, 1.0f));
}

TEST(RoundedTransformsTest, TurnsOnTheRightOfTheMatrix)
{
    const float c = RoundedCos(Angle);
    const float s = RoundedSin(Angle);
    const glm::mat4 m(2.0f, 0.0f, 0.0f, 0.0f, //
                      0.0f, 3.0f, 0.0f, 0.0f, //
                      0.0f, 0.0f, 4.0f, 0.0f, //
                      5.0f, 6.0f, 7.0f, 1.0f);

    const glm::mat4 turned = RoundedRotate(m, Angle, {0.0f, 0.0f, 1.0f});

    EXPECT_EQ(turned[0], m[0] * c + m[1] * s);
    EXPECT_EQ(turned[1], m[0] * -s + m[1] * c);
    EXPECT_EQ(turned[2], m[2]);
    EXPECT_EQ(turned[3], m[3]);
}

TEST(RoundedTransformsTest, TheAxisCanHaveAnyLength)
{
    const glm::mat4 identity(1.0f);

    EXPECT_EQ(RoundedRotate(identity, Angle, {0.0f, 0.0f, 2.0f}), RoundedRotate(identity, Angle, {0.0f, 0.0f, 1.0f}));
}

TEST(RoundedTransformsTest, ThePerspectiveIsGluPerspective)
{
    constexpr float FieldOfView = 0.6f;
    constexpr float Aspect = 1.25f;
    constexpr float NearPlane = 20.0f;
    constexpr float FarPlane = 2000.0f;
    const float tangent = RoundedTan(FieldOfView / 2.0f);

    EXPECT_EQ(RoundedPerspective(FieldOfView, Aspect, NearPlane, FarPlane),
              glm::mat4(1.0f / (Aspect * tangent), 0.0f, 0.0f, 0.0f,                         //
                        0.0f, 1.0f / tangent, 0.0f, 0.0f,                                    //
                        0.0f, 0.0f, -(FarPlane + NearPlane) / (FarPlane - NearPlane), -1.0f, //
                        0.0f, 0.0f, -(2.0f * FarPlane * NearPlane) / (FarPlane - NearPlane), 0.0f));
}

// The hash is the one Linux gives: any other system has to give it too.
TEST(RoundedTransformsTest, GivesTheSameBitsOnEverySystem)
{
    constexpr ExponentRange Angles{-8, 2};
    constexpr ExponentRange Lengths{-2, 2};
    constexpr ExponentRange FieldsOfView{-6, 0};
    std::mt19937_64 engine(20261121);
    std::vector<float> values;
    glm::mat4 view(1.0f);
    for (std::size_t i = 0; i < 20000; ++i)
    {
        const float angle = FloatFromBits(engine, Angles);
        glm::vec3 axis;
        axis.x = FloatFromBits(engine, Lengths);
        axis.y = FloatFromBits(engine, Lengths);
        axis.z = FloatFromBits(engine, Lengths);
        // Three turns in a row, like the camera's, then back to the identity.
        view = RoundedRotate(i % 3 == 0 ? glm::mat4(1.0f) : view, angle, axis);
        Append(values, view);

        const float fieldOfView = WithoutSign(FloatFromBits(engine, FieldsOfView));
        const float aspect = WithoutSign(FloatFromBits(engine, Lengths));
        Append(values, RoundedPerspective(fieldOfView, aspect, 20.0f, 2000.0f));
    }

    EXPECT_EQ(Fnv1a64(std::as_bytes(std::span(values))), 0xfb718ec3844dc1d3u);
}
} // namespace
