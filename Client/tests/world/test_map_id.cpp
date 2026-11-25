#include "Maps/MapId.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace
{
using Client::Maps::IsBloodCastle;
using Client::Maps::IsChaosCastle;
using Client::Maps::IsKalimaMap;

static_assert(IsBloodCastle(Client::Maps::FirstBloodCastleMap) && IsBloodCastle(Client::Maps::LastBloodCastleMap));
static_assert(IsChaosCastle(Client::Maps::FirstChaosCastleMap) && IsChaosCastle(Client::Maps::LastChaosCastleMap));
static_assert(IsKalimaMap(Client::Maps::FirstKalimaMap) && IsKalimaMap(Client::Maps::LastKalimaMap));
static_assert(IsKalimaMap(Client::Maps::SeventhKalimaMap));

TEST(MapIdTest, BloodCastleIsMaps11To17)
{
    EXPECT_FALSE(IsBloodCastle(10));
    EXPECT_TRUE(IsBloodCastle(11));
    EXPECT_TRUE(IsBloodCastle(17));
    EXPECT_FALSE(IsBloodCastle(18));
}

TEST(MapIdTest, ChaosCastleIsMaps18To23)
{
    EXPECT_FALSE(IsChaosCastle(17));
    EXPECT_TRUE(IsChaosCastle(18));
    EXPECT_TRUE(IsChaosCastle(23));
    EXPECT_FALSE(IsChaosCastle(24));
}

TEST(MapIdTest, KalimaIsMaps24To29And36)
{
    EXPECT_TRUE(IsKalimaMap(24));
    EXPECT_TRUE(IsKalimaMap(29));
    EXPECT_FALSE(IsKalimaMap(30));
    EXPECT_FALSE(IsKalimaMap(35));
    EXPECT_TRUE(IsKalimaMap(36));
}

TEST(MapIdTest, EachPredicateHoldsOnlyInsideItsRange)
{
    for (std::int32_t map = -1; map <= 0xff; ++map)
    {
        EXPECT_EQ(IsBloodCastle(map), map >= 11 && map <= 17) << map;
        EXPECT_EQ(IsChaosCastle(map), map >= 18 && map <= 23) << map;
        EXPECT_EQ(IsKalimaMap(map), (map >= 24 && map <= 29) || map == 36) << map;
    }
}
} // namespace
