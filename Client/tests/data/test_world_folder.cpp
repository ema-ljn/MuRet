#include "Maps/WorldFolder.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

// Each map's World<N> folder, as 0x61a560 builds it (05 § La carpeta de cada mapa). What the lab's Data\ has is
// tested by test_golden_world_folder.cpp.
namespace
{
using Client::Maps::OriginalMaps;
using Client::Maps::WorldFolder;
using Client::Maps::WorldFolderNumber;

struct MapFolder
{
    std::uint8_t map;
    std::uint16_t folder;
};

// Read at 0x61a560: map + 1, except the three event ranges (0x61a5e4..0x61a61d) and 32, which uses 9's
// (0x61a56c..0x61a57d), in OriginalMaps' order.
constexpr std::array<MapFolder, 39> ExpectedFolders = {{
    {0, 1},   {1, 2},   {2, 3},   {3, 4},   {4, 5},   {5, 6},   {6, 7},   {7, 8},   {8, 9},   {9, 10},
    {10, 11}, {11, 12}, {12, 12}, {13, 12}, {14, 12}, {15, 12}, {16, 12}, {17, 12}, {18, 19}, {19, 19},
    {20, 19}, {21, 19}, {22, 19}, {23, 19}, {24, 25}, {25, 25}, {26, 25}, {27, 25}, {28, 25}, {29, 25},
    {30, 31}, {31, 32}, {32, 10}, {33, 34}, {34, 35}, {35, 36}, {36, 25}, {54, 55}, {55, 56},
}};

TEST(WorldFolderTest, OriginalMapsAreTheGameMapsAndTheTwoScenes)
{
    ASSERT_EQ(OriginalMaps.size(), ExpectedFolders.size());
    for (std::size_t i = 0; i < OriginalMaps.size(); ++i)
    {
        EXPECT_EQ(OriginalMaps[i], ExpectedFolders[i].map) << "lugar " << i;
    }
    EXPECT_TRUE(std::is_sorted(OriginalMaps.begin(), OriginalMaps.end()));
    EXPECT_EQ(std::adjacent_find(OriginalMaps.begin(), OriginalMaps.end()), OriginalMaps.end());
}

TEST(WorldFolderTest, MapsEachMapToItsFolder)
{
    for (const MapFolder& expected : ExpectedFolders)
    {
        EXPECT_EQ(WorldFolderNumber(expected.map), expected.folder) << "mapa " << int{expected.map};
    }
}

// The range edges, in the order 0x61a560 compares them: signed, `jl` and `jg` (or `jle`) against 11 and 17, 24 and
// 29, 36 apart, and 18 and 23.
TEST(WorldFolderTest, TheEventRangesEndWhereTheOriginalCompares)
{
    EXPECT_EQ(WorldFolderNumber(10), 11);
    EXPECT_EQ(WorldFolderNumber(11), 12);
    EXPECT_EQ(WorldFolderNumber(17), 12);
    EXPECT_EQ(WorldFolderNumber(18), 19);
    EXPECT_EQ(WorldFolderNumber(23), 19);
    EXPECT_EQ(WorldFolderNumber(24), 25);
    EXPECT_EQ(WorldFolderNumber(29), 25);
    EXPECT_EQ(WorldFolderNumber(30), 31);
    EXPECT_EQ(WorldFolderNumber(35), 36);
    EXPECT_EQ(WorldFolderNumber(36), 25);
    EXPECT_EQ(WorldFolderNumber(37), 38);
}

// 0x61a56c..0x61a57d: with map 32, 0x61a560 changes the global map (0x6c9dd0) to 9 before building the folder, so it
// gives World10, 9's.
TEST(WorldFolderTest, TheMapThirtyTwoUsesTheFolderOfNine)
{
    EXPECT_EQ(WorldFolderNumber(32), WorldFolderNumber(9));
    EXPECT_EQ(WorldFolderNumber(32), 10);
    EXPECT_EQ(WorldFolder(32), "World10");
}

// Character selection sets map 54 (0x633f44, in 0x633f20) and the login 55 (0x6349b2, in 0x6349a0).
TEST(WorldFolderTest, TheSelectionMapIsWorld55)
{
    EXPECT_EQ(WorldFolderNumber(54), 55);
    EXPECT_EQ(WorldFolder(54), "World55");
    EXPECT_EQ(WorldFolder(55), "World56");
}

// 0x6c7330's "World%d", without leading zeros.
TEST(WorldFolderTest, NamesTheFolder)
{
    EXPECT_EQ(WorldFolder(0), "World1");
    EXPECT_EQ(WorldFolder(8), "World9");
    EXPECT_EQ(WorldFolder(12), "World12");
    EXPECT_EQ(WorldFolder(55), "World56");
}

// The handlers send the packet's byte without a range check (0x4c41d4, 0x4c4eab, 0x4c7817; Q-C58): from 37 on, map +
// 1, and 255 gives 256, which does not fit in a byte.
TEST(WorldFolderTest, MapsBeyondThirtySixAreMapPlusOne)
{
    EXPECT_EQ(WorldFolderNumber(37), 38);
    EXPECT_EQ(WorldFolderNumber(53), 54);
    EXPECT_EQ(WorldFolderNumber(56), 57);
    EXPECT_EQ(WorldFolderNumber(255), 256);
    EXPECT_EQ(WorldFolder(255), "World256");
    for (int map = 37; map <= std::numeric_limits<std::uint8_t>::max(); ++map)
    {
        EXPECT_EQ(WorldFolderNumber(static_cast<std::uint8_t>(map)), map + 1) << "mapa " << map;
    }
}
} // namespace
