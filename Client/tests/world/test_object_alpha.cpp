#include "World/ObjectAlpha.h"

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>

namespace Client::World
{
TEST(ObjectAlphaTest, InterpolationAsymmetric)
{
    const float target = std::bit_cast<float>(0x3f400000u);
    float current = std::bit_cast<float>(0x3e000000u);
    float ceiling = std::bit_cast<float>(0x3f800000u);
    StepObjectAlpha(target, current, ceiling, 0x00);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0x3e400000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(ceiling), 0x3e400000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(target), 0x3f400000u);
}
TEST(ObjectAlphaTest, InterpolationUnclamped)
{
    const float target = std::bit_cast<float>(0x40000000u);
    float current = std::bit_cast<float>(0x3fc00000u);
    float ceiling = std::bit_cast<float>(0x40000000u);
    StepObjectAlpha(target, current, ceiling, 0x00);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0x3fc66666u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(ceiling), 0x3fc66666u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(target), 0x40000000u);
}
TEST(ObjectAlphaTest, RiseOvershootsTarget)
{
    const float target = std::bit_cast<float>(0x3f028f5cu);
    float current = std::bit_cast<float>(0x3f000000u);
    float ceiling = std::bit_cast<float>(0x3f800000u);
    StepObjectAlpha(target, current, ceiling, 0x01);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0x3f0ccccdu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(ceiling), 0x3f0ccccdu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(target), 0x3f028f5cu);
}
TEST(ObjectAlphaTest, RiseClampsUpper)
{
    const float target = std::bit_cast<float>(0x40000000u);
    float current = std::bit_cast<float>(0x3f7d70a4u);
    float ceiling = std::bit_cast<float>(0x40000000u);
    StepObjectAlpha(target, current, ceiling, 0xff);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0x3f800000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(ceiling), 0x3f800000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(target), 0x40000000u);
}
TEST(ObjectAlphaTest, RiseRemainsNegative)
{
    const float target = std::bit_cast<float>(0x00000000u);
    float current = std::bit_cast<float>(0xbf000000u);
    float ceiling = std::bit_cast<float>(0x3f800000u);
    StepObjectAlpha(target, current, ceiling, 0x80);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0xbee66666u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(ceiling), 0xbee66666u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(target), 0x00000000u);
}
TEST(ObjectAlphaTest, FallRemainsAboveOne)
{
    const float target = std::bit_cast<float>(0x00000000u);
    float current = std::bit_cast<float>(0x40000000u);
    float ceiling = std::bit_cast<float>(0x40400000u);
    StepObjectAlpha(target, current, ceiling, 0x7f);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0x3ff9999au);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(ceiling), 0x3ff9999au);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(target), 0x00000000u);
}
TEST(ObjectAlphaTest, FallClampsLower)
{
    const float target = std::bit_cast<float>(0xbf800000u);
    float current = std::bit_cast<float>(0x3ca3d70au);
    float ceiling = std::bit_cast<float>(0x3f800000u);
    StepObjectAlpha(target, current, ceiling, 0x02);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0x00000000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(ceiling), 0x00000000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(target), 0xbf800000u);
}
TEST(ObjectAlphaTest, EqualPreservesCurrent)
{
    const float target = std::bit_cast<float>(0x3f000000u);
    float current = std::bit_cast<float>(0x3f000000u);
    float ceiling = std::bit_cast<float>(0x3e800000u);
    StepObjectAlpha(target, current, ceiling, 0x03);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0x3f000000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(ceiling), 0x3e800000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(target), 0x3f000000u);
}
TEST(ObjectAlphaTest, SignedZeroEquality)
{
    const float target = std::bit_cast<float>(0x00000000u);
    float current = std::bit_cast<float>(0x80000000u);
    float ceiling = std::bit_cast<float>(0x00000000u);
    StepObjectAlpha(target, current, ceiling, 0x01);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0x80000000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(ceiling), 0x00000000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(target), 0x00000000u);
}
TEST(ObjectAlphaTest, TargetNanFalls)
{
    const float target = std::bit_cast<float>(0x7fc12345u);
    float current = std::bit_cast<float>(0x3f000000u);
    float ceiling = std::bit_cast<float>(0x3f800000u);
    StepObjectAlpha(target, current, ceiling, 0xff);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0x3ee66666u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(ceiling), 0x3ee66666u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(target), 0x7fc12345u);
}
TEST(ObjectAlphaTest, CurrentNanClamps)
{
    const float target = std::bit_cast<float>(0x3f800000u);
    float current = std::bit_cast<float>(0x7fc54321u);
    float ceiling = std::bit_cast<float>(0x3f800000u);
    StepObjectAlpha(target, current, ceiling, 0x80);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0x00000000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(ceiling), 0x00000000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(target), 0x3f800000u);
}
TEST(ObjectAlphaTest, CeilingNanPreserved)
{
    const float target = std::bit_cast<float>(0x3f800000u);
    float current = std::bit_cast<float>(0x3f000000u);
    float ceiling = std::bit_cast<float>(0x7fc54321u);
    StepObjectAlpha(target, current, ceiling, 0x01);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0x3f0ccccdu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(ceiling), 0x7fc54321u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(target), 0x3f800000u);
}
TEST(ObjectAlphaTest, CeilingEqualPreservesNegativeZero)
{
    const float target = std::bit_cast<float>(0x00000000u);
    float current = std::bit_cast<float>(0x00000000u);
    float ceiling = std::bit_cast<float>(0x80000000u);
    StepObjectAlpha(target, current, ceiling, 0x01);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0x00000000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(ceiling), 0x80000000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(target), 0x00000000u);
}
TEST(ObjectAlphaTest, PositiveInfinityEqual)
{
    const float target = std::bit_cast<float>(0x7f800000u);
    float current = std::bit_cast<float>(0x7f800000u);
    float ceiling = std::bit_cast<float>(0x7f800000u);
    StepObjectAlpha(target, current, ceiling, 0x01);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0x7f800000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(ceiling), 0x7f800000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(target), 0x7f800000u);
}
TEST(ObjectAlphaTest, NegativeInfinityEqual)
{
    const float target = std::bit_cast<float>(0xff800000u);
    float current = std::bit_cast<float>(0xff800000u);
    float ceiling = std::bit_cast<float>(0x00000000u);
    StepObjectAlpha(target, current, ceiling, 0x01);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0xff800000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(ceiling), 0xff800000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(target), 0xff800000u);
}
TEST(ObjectAlphaTest, RiseFromNegativeInfinity)
{
    const float target = std::bit_cast<float>(0x00000000u);
    float current = std::bit_cast<float>(0xff800000u);
    float ceiling = std::bit_cast<float>(0x00000000u);
    StepObjectAlpha(target, current, ceiling, 0x01);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0xff800000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(ceiling), 0xff800000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(target), 0x00000000u);
}
TEST(ObjectAlphaTest, FallFromPositiveInfinity)
{
    const float target = std::bit_cast<float>(0x00000000u);
    float current = std::bit_cast<float>(0x7f800000u);
    float ceiling = std::bit_cast<float>(0x7f800000u);
    StepObjectAlpha(target, current, ceiling, 0x01);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0x7f800000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(ceiling), 0x7f800000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(target), 0x00000000u);
}
TEST(ObjectAlphaTest, SubnormalInterpolation)
{
    const float target = std::bit_cast<float>(0x00000001u);
    float current = std::bit_cast<float>(0x00000002u);
    float ceiling = std::bit_cast<float>(0x00000001u);
    StepObjectAlpha(target, current, ceiling, 0x00);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0x00000002u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(ceiling), 0x00000001u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(target), 0x00000001u);
}
TEST(ObjectAlphaTest, InterpolationCancellation)
{
    const float target = std::bit_cast<float>(0x3f800000u);
    float current = std::bit_cast<float>(0x4b800000u);
    float ceiling = std::bit_cast<float>(0x3f800000u);
    StepObjectAlpha(target, current, ceiling, 0x00);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0x4b666666u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(ceiling), 0x3f800000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(target), 0x3f800000u);
}
TEST(ObjectAlphaTest, InterpolationNanSkipsCeiling)
{
    const float target = std::bit_cast<float>(0x7fc12345u);
    float current = 0.5f;
    float ceiling = 0.75f;
    StepObjectAlpha(target, current, ceiling, 0);
    EXPECT_TRUE(std::isnan(current));
    EXPECT_EQ(ceiling, 0.75f);
}
TEST(ObjectAlphaTest, BorrowedTargetAliasesCeiling)
{
    float targetAndCeiling = 0.75f;
    float current = 0.125f;
    StepObjectAlpha(targetAndCeiling, current, targetAndCeiling, 0);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0x3e400000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(targetAndCeiling), 0x3e400000u);
}
TEST(ObjectAlphaTest, BorrowedCurrentAliasesCeiling)
{
    const float target = 0.75f;
    float current = 0.125f;
    StepObjectAlpha(target, current, current, 0);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0x3e400000u);
}
TEST(ObjectAlphaTest, BorrowedTargetAliasesCurrent)
{
    float current = 0.5f;
    float ceiling = 1.0f;
    StepObjectAlpha(current, current, ceiling, 0xff);
    EXPECT_EQ(current, 0.5f);
    EXPECT_EQ(ceiling, 0.5f);
}
TEST(ObjectAlphaTest, RejectsPrecision24)
{
    const float target = std::bit_cast<float>(0x5517ac25u);
    float current = std::bit_cast<float>(0x52d8c9c4u);
    float ceiling = 0.0f;
    StepObjectAlpha(target, current, ceiling, 0);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0x53aa1d83u);
}
TEST(ObjectAlphaTest, SignalingNanCurrentClampsToPositiveZero)
{
    const float target = 1.0f;
    float current = std::bit_cast<float>(0x7f812345u);
    float ceiling = 1.0f;
    StepObjectAlpha(target, current, ceiling, 0xff);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(ceiling), 0u);
}
TEST(ObjectAlphaTest, SignalingNanCeilingKeepsItsBits)
{
    const float target = 1.0f;
    float current = 0.5f;
    float ceiling = std::bit_cast<float>(0x7f812345u);
    StepObjectAlpha(target, current, ceiling, 0xff);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0x3f0ccccdu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(ceiling), 0x7f812345u);
}
TEST(ObjectAlphaTest, AllNonzeroRawModesShareStepBehavior)
{
    const float target = 0.0f;
    for (unsigned mode = 1; mode <= 0xff; ++mode)
    {
        float current = 0.5f;
        float ceiling = 1.0f;
        StepObjectAlpha(target, current, ceiling, static_cast<std::uint8_t>(mode));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0x3ee66666u) << mode;
        EXPECT_EQ(std::bit_cast<std::uint32_t>(ceiling), 0x3ee66666u) << mode;
    }
}
TEST(ObjectAlphaTest, RepeatedStepsReverseAfterOvershootingTarget)
{
    const float target = std::bit_cast<float>(0x3f028f5cu);
    float current = 0.5f;
    float ceiling = 1.0f;
    StepObjectAlpha(target, current, ceiling, 1);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0x3f0ccccdu);
    StepObjectAlpha(target, current, ceiling, 1);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(current), 0x3f000000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(ceiling), 0x3f000000u);
}
} // namespace Client::World
