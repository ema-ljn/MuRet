#include "Data/WorldFolder.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

// Client::Data: la carpeta World<N> de cada mapa, como la arma 0x61a560 (05 § La carpeta de cada
// mapa). Lo que hay en el Data\ del laboratorio lo prueba test_golden_world_folder.cpp.
namespace
{
using Client::Data::OriginalMaps;
using Client::Data::WorldFolder;
using Client::Data::WorldFolderNumber;

// Un mapa y el N de su carpeta.
struct MapFolder
{
    std::uint8_t map;
    std::uint16_t folder;
};

// La tabla del paso 1 de la CL5.4, leída en 0x61a560 (CL5.R): mapa + 1, salvo los tres rangos de
// evento (0x61a5e4..0x61a61d) y el 32, que usa la del 9 (0x61a56c..0x61a57d), en el orden de
// OriginalMaps.
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

// Los bordes de los rangos, en el orden en que 0x61a560 los compara: con signo, `jl` y `jg` (o
// `jle`) contra 11 y 17, 24 y 29, el 36 aparte, y 18 y 23.
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

// 0x61a56c..0x61a57d: con el mapa 32, 0x61a560 cambia el mapa global (0x6c9dd0) por el 9 antes
// de armar la carpeta, así que da World10, la del 9.
TEST(WorldFolderTest, TheMapThirtyTwoUsesTheFolderOfNine)
{
    EXPECT_EQ(WorldFolderNumber(32), WorldFolderNumber(9));
    EXPECT_EQ(WorldFolderNumber(32), 10);
    EXPECT_EQ(WorldFolder(32), "World10");
}

// La selección de personaje pone el mapa 54 (0x633f44, en 0x633f20) y el login el 55 (0x6349b2, en
// 0x6349a0).
TEST(WorldFolderTest, TheSelectionMapIsWorld55)
{
    EXPECT_EQ(WorldFolderNumber(54), 55);
    EXPECT_EQ(WorldFolder(54), "World55");
    EXPECT_EQ(WorldFolder(55), "World56");
}

// El "World%d" de 0x6c7330, sin ceros adelante.
TEST(WorldFolderTest, NamesTheFolder)
{
    EXPECT_EQ(WorldFolder(0), "World1");
    EXPECT_EQ(WorldFolder(8), "World9");
    EXPECT_EQ(WorldFolder(12), "World12");
    EXPECT_EQ(WorldFolder(55), "World56");
}

// Los handlers mandan el byte del paquete sin mirar el rango (0x4c41d4, 0x4c4eab, 0x4c7817; Q-C58):
// del 37 en adelante, el mapa + 1, y el 255 da 256, que no entra en un byte.
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
