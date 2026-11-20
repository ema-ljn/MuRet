#include "Scenes/MapNames.h"

#include <gtest/gtest.h>

#include <cstdint>

// 0x5f0b40: el texto de Text.bmd con el nombre del mapa, en el orden de sus comparaciones.
namespace
{
using Client::Scenes::MapNameText;

TEST(MapNamesTest, TheEventMapsShareTheirText)
{
    EXPECT_EQ(MapNameText(0x0a), 55u);
    for (std::int32_t map = 0x0b; map <= 0x11; ++map)
        EXPECT_EQ(MapNameText(map), 56u) << map;
    for (std::int32_t map = 0x12; map <= 0x17; ++map)
        EXPECT_EQ(MapNameText(map), 57u) << map;
    for (std::int32_t map = 0x18; map <= 0x1d; ++map)
        EXPECT_EQ(MapNameText(map), 58u) << map;
    EXPECT_EQ(MapNameText(0x24), 58u);
}

TEST(MapNamesTest, TheLateMapsHaveTheirOwnTextAndTheRestFollowThirty)
{
    EXPECT_EQ(MapNameText(0x1e), 669u);
    EXPECT_EQ(MapNameText(0x1f), 59u);
    EXPECT_EQ(MapNameText(0x20), 39u);
    EXPECT_EQ(MapNameText(0x21), 1850u);
    EXPECT_EQ(MapNameText(0x22), 1851u);
    EXPECT_EQ(MapNameText(0), 30u);
    EXPECT_EQ(MapNameText(2), 32u);
    EXPECT_EQ(MapNameText(9), 39u);
    EXPECT_EQ(MapNameText(0x23), 65u);
}
} // namespace
