#include "Math/X87.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace
{
using Client::Math::Bits;
using Client::Math::D;
using Client::Math::F32;
using Client::Math::Truncate;

TEST(X87Test, TruncateIsFtolLow)
{
    EXPECT_EQ(Truncate(std::numeric_limits<double>::quiet_NaN()), 0);
    EXPECT_EQ(Truncate(std::numeric_limits<double>::infinity()), 0);
    EXPECT_EQ(Truncate(0x1p40 + 5), 5);
    EXPECT_EQ(Truncate(-1.5), -1);
    EXPECT_EQ(Truncate(2147483648.0), std::numeric_limits<std::int32_t>::min());
}

TEST(X87Test, F32RoundsLikeTheStore)
{
    EXPECT_EQ(F32(0.1), 0.1f);
    EXPECT_EQ(F32(D(0.1f)), 0.1f);
}

TEST(X87Test, BitsIsTheFloatPattern)
{
    EXPECT_EQ(Bits(1.0f), 0x3F800000u);
    EXPECT_EQ(Bits(-0.0f), 0x80000000u);
}
} // namespace
