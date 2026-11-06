#include "World/TerrainWalls.h"

#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>

namespace
{
TEST(TerrainWallsTest, TheWallsLoseTheCharacterBitEveryStep)
{
    Client::World::TerrainWalls walls;
    walls.cells.fill(0xffff);
    walls.Rebuild({});
    for (std::size_t i = 0; i < walls.cells.size(); ++i)
    {
        ASSERT_EQ(walls.cells[i], 0xfffd) << i;
    }
}

TEST(TerrainWallsTest, OnlyTheLiveNonDyingCharactersOutsideKindEightOccupyTheirLogicalCell)
{
    Client::World::TerrainWalls walls;
    walls.cells.fill(0x104);
    const std::array occupants{
        Client::World::TerrainOccupant{.logicalX = 1, .logicalY = 2, .live = true},
        Client::World::TerrainOccupant{.logicalX = 3, .logicalY = 4, .live = false},
        Client::World::TerrainOccupant{.logicalX = 5, .logicalY = 6, .live = true, .dying = true},
        Client::World::TerrainOccupant{.logicalX = 7, .logicalY = 8, .live = true, .kind = 8},
        Client::World::TerrainOccupant{.logicalX = -1, .logicalY = 256, .live = true},
    };
    walls.Rebuild(occupants);
    EXPECT_EQ(walls.cells[Client::Data::Terrain::CellIndex(1, 2)], 0x106);
    EXPECT_EQ(walls.cells[Client::Data::Terrain::CellIndex(3, 4)], 0x104);
    EXPECT_EQ(walls.cells[Client::Data::Terrain::CellIndex(5, 6)], 0x104);
    EXPECT_EQ(walls.cells[Client::Data::Terrain::CellIndex(7, 8)], 0x104);
    // 0x5f82a0 masquea ambas coordenadas con 0xff, también las negativas.
    EXPECT_EQ(walls.cells[Client::Data::Terrain::CellIndex(255, 0)], 0x106);
    walls.Rebuild({});
    EXPECT_EQ(walls.cells[Client::Data::Terrain::CellIndex(1, 2)], 0x104);
}

using GoldenTerrainWalls = Client::Tests::GoldenDataTest;

// El 2 en (221,11) es un dato del .att original, no una deducción del ASM (06 § Caminar, Q-C170).
// GoldenDataTest exige los assets del repo; si se elige otra Data, también debe traer este archivo.
TEST_F(GoldenTerrainWalls, TheLorenciaFileCharacterBitDisappearsOnTheFirstStep)
{
    const auto path = DataFile("World1\\EncTerrain1.att");
    ASSERT_TRUE(path);
    const auto file = Client::Tests::ReadFileBytes(*path);
    ASSERT_TRUE(file);
    const auto attributes = Client::Data::Terrain::LoadTerrainAttributes(std::as_bytes(std::span(*file)), 1);
    ASSERT_TRUE(attributes.HasValue());
    Client::World::TerrainWalls walls{.cells = attributes.Value()->cells};
    ASSERT_EQ(walls.cells[Client::Data::Terrain::CellIndex(221, 11)], 2);
    walls.Rebuild({});
    EXPECT_EQ(walls.cells[Client::Data::Terrain::CellIndex(221, 11)], 0);
}
} // namespace
