#include "World/TerrainWalls.h"

#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

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

// 0x42a110(0, 0, 0, 1): con cada celda de 0x6c72bc, si la de abajo tiene el 0x04 en su byte bajo, lo saca de
// las cuatro de x − 1 a x + 2 en esa fila; las demás quedan. El byte alto no cuenta para mirar.
TEST(TerrainWallsTest, TheGatesOpenOnlyBelowAWalledGateCell)
{
    auto cells = std::make_unique<Client::Data::Terrain::TerrainLayer<std::uint16_t>>();
    cells->fill(0x104);
    (*cells)[Client::Data::Terrain::CellIndex(93, 115)] = 0x400;
    Client::World::OpenTheGates(*cells);
    for (const Client::World::GateCell& gate : Client::World::GateCells)
    {
        const auto below = static_cast<std::uint8_t>(gate.y + 1);
        const bool walled = !(gate.x == 93 && gate.y == 114);
        for (int dx = -2; dx <= 3; ++dx)
        {
            const auto x = static_cast<std::uint8_t>(gate.x + dx);
            auto expected = static_cast<std::uint16_t>(walled && dx >= -1 && dx <= 2 ? 0x100 : 0x104);
            if (!walled && dx == 0)
            {
                expected = std::uint16_t{0x400};
            }
            EXPECT_EQ((*cells)[Client::Data::Terrain::CellIndex(x, below)], expected)
                << static_cast<int>(x) << ", " << static_cast<int>(below);
        }
        EXPECT_EQ((*cells)[Client::Data::Terrain::CellIndex(gate.x, gate.y)], 0x104);
    }
}

// Con el .att del repo (*simulado*): en Lorencia (World1), la puerta (119, 114) tiene la pared en (119, 115) y
// las cuatro de 118 a 121 pasan de 0x05 a 0x01; en Devias (World3), la misma y la (93, 204), de 0x04 a 0. Las
// demás no tienen la pared debajo, y no cambia nada más.
TEST_F(GoldenTerrainWalls, TheGatesOpenInLorenciaAndDevias)
{
    struct Case
    {
        std::uint8_t world;
        std::array<bool, 6> walled;
        std::uint16_t before;
        std::uint16_t after;
    };
    for (const Case& probe : {Case{1, {false, false, true, false, false, false}, 0x05, 0x01},
                              Case{3, {false, false, true, false, false, true}, 0x04, 0x00}})
    {
        SCOPED_TRACE(static_cast<int>(probe.world));
        const std::string world = std::to_string(probe.world);
        const auto path = DataFile("World" + world + "\\EncTerrain" + world + ".att");
        ASSERT_TRUE(path);
        const auto file = Client::Tests::ReadFileBytes(*path);
        ASSERT_TRUE(file);
        const auto attributes =
            Client::Data::Terrain::LoadTerrainAttributes(std::as_bytes(std::span(*file)), probe.world);
        ASSERT_TRUE(attributes.HasValue());
        const auto original =
            std::make_unique<Client::Data::Terrain::TerrainLayer<std::uint16_t>>(attributes.Value()->cells);
        auto cells = std::make_unique<Client::Data::Terrain::TerrainLayer<std::uint16_t>>(*original);
        Client::World::OpenTheGates(*cells);
        std::size_t walled = 0;
        for (std::size_t k = 0; k < Client::World::GateCells.size(); ++k)
        {
            if (!probe.walled[k])
            {
                continue;
            }
            ++walled;
            const Client::World::GateCell& gate = Client::World::GateCells[k];
            const auto below = static_cast<std::uint8_t>(gate.y + 1);
            for (int dx = -1; dx <= 2; ++dx)
            {
                const std::size_t cell =
                    Client::Data::Terrain::CellIndex(static_cast<std::uint8_t>(gate.x + dx), below);
                EXPECT_EQ((*original)[cell], probe.before) << k << " " << dx;
                EXPECT_EQ((*cells)[cell], probe.after) << k << " " << dx;
            }
        }
        std::size_t changed = 0;
        for (std::size_t cell = 0; cell < cells->size(); ++cell)
        {
            changed += (*cells)[cell] != (*original)[cell] ? 1u : 0u;
        }
        EXPECT_EQ(changed, 4u * walled);
    }
}
} // namespace
