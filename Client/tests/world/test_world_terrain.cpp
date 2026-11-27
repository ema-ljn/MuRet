#include "Terrain/WorldTerrain.h"

#include "Data/Terrain/TerrainGrid.h"
#include "Data/Textures/Sampling.h"

#include "support/GoldenDir.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>

// Client::Terrain::LoadWorldTerrain and the tile table of 0x61a560: what a world's terrain reads from its folder
// (06 § Terreno and § El terreno del login).
namespace
{
using Client::Data::Textures::Filter;
using Client::Data::Textures::Wrap;
using Client::Terrain::AcceptBoxTexture;
using Client::Terrain::LoadWorldTerrain;
using Client::Terrain::WorldGrassTextures;
using Client::Terrain::WorldTileTextures;

constexpr std::uint8_t Login = 55;

// The 14 login tiles, from World56\, in slots 0x23 to 0x30, with GL_NEAREST and repeating; only TileGrass01 warns
// if it is missing (the sixth argument of 0x63b870).
TEST(WorldTerrainTest, World56HasTheTilesOfTheOriginal)
{
    const auto tiles = WorldTileTextures(Login);
    ASSERT_EQ(tiles.size(), 14u);
    EXPECT_EQ(tiles[0].path, "World56\\TileGrass01.jpg");
    EXPECT_EQ(tiles[5].path, "World56\\TileWater01.jpg");
    EXPECT_EQ(tiles[13].path, "World56\\TileRock07.jpg");
    for (std::size_t k = 0; k < tiles.size(); ++k)
    {
        EXPECT_EQ(tiles[k].slot, 0x23 + k);
        EXPECT_EQ(tiles[k].sampling.filter, Filter::Nearest);
        EXPECT_EQ(tiles[k].sampling.wrap, Wrap::Repeat);
        EXPECT_EQ(tiles[k].required, k == 0);
    }
}

// Maps 24 to 29 and 36 build their tiles in 0x4377a0: the table has none of them.
TEST(WorldTerrainTest, TheMapsWithTheirOwnTilesHaveNone)
{
    for (const std::uint8_t map : {std::uint8_t{24}, std::uint8_t{29}, std::uint8_t{36}})
    {
        EXPECT_TRUE(WorldTileTextures(map).empty()) << static_cast<int>(map);
    }
    EXPECT_EQ(WorldTileTextures(23).size(), 14u);
    EXPECT_EQ(WorldTileTextures(30).size(), 14u);
}

// 0x61abb4..0x61ac36: after the tiles, World<N>\TileGrass01..03.tga in 0x32..0x34 with 0x63bd00, GL_NEAREST and
// repeating, without warning if they are missing. In Lorencia (World1) and in Devias (World3).
TEST(WorldTerrainTest, TheGrassComesAfterTheTiles)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        const auto grass = WorldGrassTextures(map);
        const std::string folder = map == 0 ? "World1" : "World3";
        ASSERT_EQ(grass.size(), 3u);
        for (std::size_t k = 0; k < grass.size(); ++k)
        {
            EXPECT_EQ(grass[k].path, folder + "\\TileGrass0" + std::to_string(k + 1) + ".tga");
            EXPECT_EQ(grass[k].slot, 0x32 + k);
            EXPECT_EQ(grass[k].sampling.filter, Filter::Nearest);
            EXPECT_EQ(grass[k].sampling.wrap, Wrap::Repeat);
            EXPECT_FALSE(grass[k].required);
        }
    }
    EXPECT_TRUE(WorldGrassTextures(24).empty());
    EXPECT_TRUE(WorldGrassTextures(36).empty());
}

// Q-C19: slot 0x122 is Interface\accept_box01.jpg, with GL_NEAREST and GL_CLAMP, as 0x61ae60 uploads it.
TEST(WorldTerrainTest, TheSlot0x122IsTheAcceptBox)
{
    const auto box = AcceptBoxTexture();
    EXPECT_EQ(box.path, "Interface\\accept_box01.jpg");
    EXPECT_EQ(box.slot, 0x122);
    EXPECT_EQ(box.sampling.filter, Filter::Nearest);
    EXPECT_EQ(box.sampling.wrap, Wrap::Clamp);
    EXPECT_TRUE(box.required);
}

// Q-C27: a missing MAP does not warn; without a folder the fatal ATT is reached.
TEST(WorldTerrainTest, AMissingWorldNamesTheFatalAttributes)
{
    const Client::Tests::TemporaryFolder empty("world-terrain");
    const auto terrain = LoadWorldTerrain(empty.Path(), Login);
    ASSERT_FALSE(terrain.HasValue());
    EXPECT_NE(terrain.Error().find("World56\\EncTerrain56.att"), std::string::npos) << terrain.Error();
}

class GoldenWorldTerrain : public Client::Tests::GoldenDataTest
{
};

// The login world, with the repo's Data\: the layers, the attributes (the character's cell with the bit 0x08,
// 06 § El terreno del login), the heights times 3 and the light.
TEST_F(GoldenWorldTerrain, TheLoginWorldLoads)
{
    const auto terrain = LoadWorldTerrain(m_dataRoot, Login);
    ASSERT_TRUE(terrain.HasValue()) << terrain.Error();
    const auto& world = terrain.Value();
    ASSERT_NE(world.mapping, nullptr);
    ASSERT_NE(world.attributes, nullptr);
    ASSERT_NE(world.heights, nullptr);
    ASSERT_NE(world.lighting, nullptr);
    EXPECT_NE(world.attributes->cells[Client::Data::Terrain::CellIndex(106, 141)] & 0x08, 0);
    // On cell (0, 0) of map 55 the height byte is 150: 450 times 3.
    EXPECT_EQ(world.heights->cells[0], 450.0f);
}
} // namespace
