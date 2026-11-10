#include "World/WorldTerrain.h"

#include "Data/Terrain/TerrainGrid.h"
#include "Data/Textures/Sampling.h"

#include "support/GoldenDir.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>

// Client::World::LoadWorldTerrain y la tabla de los tiles de 0x61a560: lo que el terreno de un mundo
// lee de su carpeta (06 § Terreno y § El terreno del login).
namespace
{
using Client::Data::Textures::Filter;
using Client::Data::Textures::Wrap;
using Client::World::AcceptBoxTexture;
using Client::World::LoadWorldTerrain;
using Client::World::WorldGrassTextures;
using Client::World::WorldTileTextures;

constexpr std::uint8_t Login = 55;

// Los 14 tiles del login, de World56\, en los slots 0x23 a 0x30, con GL_NEAREST y repitiendo; solo
// TileGrass01 avisa si falta (el sexto argumento de 0x63b870).
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

// Los mapas 24 a 29 y 36 arman sus tiles en 0x4377a0: la tabla no tiene ninguno.
TEST(WorldTerrainTest, TheMapsWithTheirOwnTilesHaveNone)
{
    for (const std::uint8_t map : {std::uint8_t{24}, std::uint8_t{29}, std::uint8_t{36}})
    {
        EXPECT_TRUE(WorldTileTextures(map).empty()) << static_cast<int>(map);
    }
    EXPECT_EQ(WorldTileTextures(23).size(), 14u);
    EXPECT_EQ(WorldTileTextures(30).size(), 14u);
}

// 0x61abb4..0x61ac36: después de los tiles, World<N>\TileGrass01..03.tga en 0x32..0x34 con 0x63bd00,
// GL_NEAREST y repitiendo, sin avisar si faltan. En Lorencia (World1) y en Devias (World3).
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

// Q-C19: el slot 0x122 es Interface\accept_box01.jpg, con GL_NEAREST y GL_CLAMP, como lo sube 0x61ae60.
TEST(WorldTerrainTest, TheSlot0x122IsTheAcceptBox)
{
    const auto box = AcceptBoxTexture();
    EXPECT_EQ(box.path, "Interface\\accept_box01.jpg");
    EXPECT_EQ(box.slot, 0x122);
    EXPECT_EQ(box.sampling.filter, Filter::Nearest);
    EXPECT_EQ(box.sampling.wrap, Wrap::Clamp);
    EXPECT_TRUE(box.required);
}

// Sin la carpeta del mundo, el error nombra el primer archivo que pide, como lo pide el original.
TEST(WorldTerrainTest, AMissingWorldNamesTheMap)
{
    const Client::Tests::TemporaryFolder empty("world-terrain");
    const auto terrain = LoadWorldTerrain(empty.Path(), Login);
    ASSERT_FALSE(terrain.HasValue());
    EXPECT_NE(terrain.Error().find("World56\\EncTerrain56.map"), std::string::npos) << terrain.Error();
}

class GoldenWorldTerrain : public Client::Tests::GoldenDataTest
{
};

// El mundo del login, con el Data\ del repo: las capas, los atributos (la celda del personaje con el
// bit 0x08, 06 § El terreno del login), las alturas por 3 y la luz.
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
    // En la celda (0, 0) del mapa 55 el byte de la altura es 150: 450 por 3.
    EXPECT_EQ(world.heights->cells[0], 450.0f);
}
} // namespace
