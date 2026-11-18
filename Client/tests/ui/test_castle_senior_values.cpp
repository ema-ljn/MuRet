#include "UI/CastleSiegeState.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace
{
struct LifeCase
{
    std::int32_t life;
    std::int32_t maximum;
    float width;
    std::uint32_t bits;
};

TEST(CastleSeniorValuesTest, TheLifeRatioIsStoredBeforeMultiplyingWithoutRoundingTheIntegerOperands)
{
    const std::array<LifeCase, 4> cases{{{9, 500000, 6, 0x38e27e10},
                                         {16777217, 16777219, 24, 0x41bffffe},
                                         {-16777217, 16777219, 18, 0xc18fffff},
                                         {2147483647, 16777217, 24, 0x453fffff}}};
    for (const auto& test : cases)
        EXPECT_EQ(std::bit_cast<std::uint32_t>(Client::UI::NpcLifeBarWidth(test.life, test.maximum, test.width)),
                  test.bits);
}

TEST(CastleSeniorValuesTest, ZeroMaximumKeepsTheNativeMaskedFloatingPointDivision)
{
    EXPECT_TRUE(std::isinf(Client::UI::NpcLifeBarWidth(1, 0, 6)));
    EXPECT_TRUE(std::isnan(Client::UI::NpcLifeBarWidth(0, 0, 6)));
}

TEST(CastleSeniorValuesTest, RecoveryIncreaseUsesTheTypeBeforeTheLevelForFlatRecordAliases)
{
    for (const std::int32_t level : {-1, 0, 1, 2, 3, 2147483647})
    {
        EXPECT_EQ(Client::UI::NpcRecoveryIncrease(0x115, level), 0);
        EXPECT_EQ(Client::UI::NpcRecoveryIncrease(0x120, level), -1);
        EXPECT_EQ(Client::UI::NpcRecoveryIncrease(0x11b, level), level >= 0 && level <= 2 ? 1 : 0);
    }
}
} // namespace
