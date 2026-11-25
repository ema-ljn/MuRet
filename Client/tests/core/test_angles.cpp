#include "Math/Angles.h"

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>

namespace
{
namespace Math = Client::Math;

constexpr std::uint32_t Bits(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

constexpr std::uint64_t Bits(double value)
{
    return std::bit_cast<std::uint64_t>(value);
}

TEST(AnglesTest, EachTurnHasItsDegrees)
{
    EXPECT_EQ(Math::FullTurn, 360);
    EXPECT_EQ(Bits(Math::FullTurnAsFloat), 0x43b40000u);
    EXPECT_EQ(Bits(Math::HalfTurnAsFloat), 0x43340000u);
    EXPECT_EQ(Bits(Math::QuarterTurnAsFloat), 0x42b40000u);
    EXPECT_EQ(Bits(Math::FullTurnAsDouble), 0x4076800000000000ull);
    EXPECT_EQ(Bits(Math::HalfTurnAsDouble), 0x4066800000000000ull);
    EXPECT_EQ(Bits(Math::QuarterTurnAsDouble), 0x4056800000000000ull);
}

TEST(AnglesTest, TheTwoDegreeToRadianFactorsDifferInTheLastBit)
{
    EXPECT_EQ(Bits(Math::DegreesToRadians), 0x3c8efa35u);
    EXPECT_EQ(Bits(Math::DegreesToRadiansRoundedUp), 0x3c8efa36u);
    EXPECT_EQ(Bits(Math::DegreesToRadiansAsDouble), 0x3f91df46a0000000ull);
}
} // namespace
