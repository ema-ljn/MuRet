#include "Math/FloatConstants.h"

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

TEST(FloatConstantsTest, EachFractionHasTheBitsOfTheOriginal)
{
    EXPECT_EQ(Bits(Math::Half), 0x3f000000u);
    EXPECT_EQ(Bits(Math::Third), 0x3eaaaaabu);
    EXPECT_EQ(Bits(Math::Quarter), 0x3e800000u);
    EXPECT_EQ(Bits(Math::Fifth), 0x3e4ccccdu);
    EXPECT_EQ(Bits(Math::Eighth), 0x3e000000u);
    EXPECT_EQ(Bits(Math::Tenth), 0x3dcccccdu);
    EXPECT_EQ(Bits(Math::Twentieth), 0x3d4ccccdu);
    EXPECT_EQ(Bits(Math::Fiftieth), 0x3ca3d70au);
    EXPECT_EQ(Bits(Math::Hundredth), 0x3c23d70au);
    EXPECT_EQ(Bits(Math::Thousandth), 0x3a83126fu);
}

TEST(FloatConstantsTest, EachDoubleIsItsFloatWidened)
{
    EXPECT_EQ(Bits(Math::HalfAsDouble), 0x3fe0000000000000ull);
    EXPECT_EQ(Bits(Math::ThirdAsDouble), 0x3fd5555560000000ull);
    EXPECT_EQ(Bits(Math::QuarterAsDouble), 0x3fd0000000000000ull);
    EXPECT_EQ(Bits(Math::FifthAsDouble), 0x3fc99999a0000000ull);
    EXPECT_EQ(Bits(Math::EighthAsDouble), 0x3fc0000000000000ull);
    EXPECT_EQ(Bits(Math::TenthAsDouble), 0x3fb99999a0000000ull);
    EXPECT_EQ(Bits(Math::TwentiethAsDouble), 0x3fa99999a0000000ull);
    EXPECT_EQ(Bits(Math::FiftiethAsDouble), 0x3f947ae140000000ull);
    EXPECT_EQ(Bits(Math::HundredthAsDouble), 0x3f847ae140000000ull);
    EXPECT_EQ(Bits(Math::ThousandthAsDouble), 0x3f50624de0000000ull);
}

TEST(FloatConstantsTest, TheDecimalLiteralsAreTheSameFloats)
{
    EXPECT_EQ(Bits(0.5f), Bits(Math::Half));
    EXPECT_EQ(Bits(0.2f), Bits(Math::Fifth));
    EXPECT_EQ(Bits(0.1f), Bits(Math::Tenth));
    EXPECT_EQ(Bits(0.05f), Bits(Math::Twentieth));
    EXPECT_EQ(Bits(0.01f), Bits(Math::Hundredth));
    EXPECT_EQ(Bits(0.001f), Bits(Math::Thousandth));
    EXPECT_EQ(Bits(0.5), Bits(Math::HalfAsDouble));
    EXPECT_EQ(Bits(0.25), Bits(Math::QuarterAsDouble));
    EXPECT_EQ(Bits(0.125), Bits(Math::EighthAsDouble));
}
} // namespace
