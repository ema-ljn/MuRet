#include "Pathfinding/TerrainWalls.h"

#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace
{
TEST(TerrainWallsTest, TheWallsLoseTheCharacterBitEveryStep)
{
    Client::Pathfinding::TerrainWalls walls;
    walls.cells.fill(0xffff);
    walls.Rebuild({});
    for (std::size_t i = 0; i < walls.cells.size(); ++i)
    {
        ASSERT_EQ(walls.cells[i], 0xfffd) << i;
    }
}

TEST(TerrainWallsTest, OnlyTheLiveNonDyingCharactersOutsideKindEightOccupyTheirLogicalCell)
{
    Client::Pathfinding::TerrainWalls walls;
    walls.cells.fill(0x104);
    const std::array occupants{
        Client::Pathfinding::TerrainOccupant{.logicalX = 1, .logicalY = 2, .live = true},
        Client::Pathfinding::TerrainOccupant{.logicalX = 3, .logicalY = 4, .live = false},
        Client::Pathfinding::TerrainOccupant{.logicalX = 5, .logicalY = 6, .live = true, .dying = true},
        Client::Pathfinding::TerrainOccupant{.logicalX = 7, .logicalY = 8, .live = true, .kind = 8},
        Client::Pathfinding::TerrainOccupant{.logicalX = -1, .logicalY = 256, .live = true},
    };
    walls.Rebuild(occupants);
    EXPECT_EQ(walls.cells[Client::Data::Terrain::CellIndex(1, 2)], 0x106);
    EXPECT_EQ(walls.cells[Client::Data::Terrain::CellIndex(3, 4)], 0x104);
    EXPECT_EQ(walls.cells[Client::Data::Terrain::CellIndex(5, 6)], 0x104);
    EXPECT_EQ(walls.cells[Client::Data::Terrain::CellIndex(7, 8)], 0x104);
    // 0x5f82a0 masks both coordinates with 0xff, negative ones too.
    EXPECT_EQ(walls.cells[Client::Data::Terrain::CellIndex(255, 0)], 0x106);
    walls.Rebuild({});
    EXPECT_EQ(walls.cells[Client::Data::Terrain::CellIndex(1, 2)], 0x104);
}

using GoldenTerrainWalls = Client::Tests::GoldenDataTest;

// The 2 at (221,11) is a value from the original .att, not a deduction from the ASM (06 § Caminar, Q-C170).
// GoldenDataTest requires the repo's assets; another Data must also bring this file.
TEST_F(GoldenTerrainWalls, TheLorenciaFileCharacterBitDisappearsOnTheFirstStep)
{
    const auto path = DataFile("World1\\EncTerrain1.att");
    ASSERT_TRUE(path);
    const auto file = Client::Tests::ReadFileBytes(*path);
    ASSERT_TRUE(file);
    const auto attributes = Client::Data::Terrain::LoadTerrainAttributes(std::as_bytes(std::span(*file)), 1);
    ASSERT_TRUE(attributes.HasValue());
    Client::Pathfinding::TerrainWalls walls{.cells = attributes.Value()->cells};
    ASSERT_EQ(walls.cells[Client::Data::Terrain::CellIndex(221, 11)], 2);
    walls.Rebuild({});
    EXPECT_EQ(walls.cells[Client::Data::Terrain::CellIndex(221, 11)], 0);
}

// 0x42a110(0, 0, 0, 1): for each cell of 0x6c72bc, if the one below has 0x04 in its low byte, it clears it from the
// four from x − 1 to x + 2 in that row; the rest stay. The high byte does not count for the check.
TEST(TerrainWallsTest, TheGatesOpenOnlyBelowAWalledGateCell)
{
    auto cells = std::make_unique<Client::Data::Terrain::TerrainLayer<std::uint16_t>>();
    cells->fill(0x104);
    (*cells)[Client::Data::Terrain::CellIndex(93, 115)] = 0x400;
    Client::Pathfinding::OpenTheGates(*cells);
    for (const Client::Pathfinding::GateCell& gate : Client::Pathfinding::GateCells)
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

// With the repo's .att (*simulated*): in Lorencia (World1), the gate (119, 114) has the wall at (119, 115) and the four
// from 118 to 121 go from 0x05 to 0x01; in Devias (World3), the same and (93, 204), from 0x04 to 0. The others have
// no wall below, and nothing else changes.
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
        Client::Pathfinding::OpenTheGates(*cells);
        std::size_t walled = 0;
        for (std::size_t k = 0; k < Client::Pathfinding::GateCells.size(); ++k)
        {
            if (!probe.walled[k])
            {
                continue;
            }
            ++walled;
            const Client::Pathfinding::GateCell& gate = Client::Pathfinding::GateCells[k];
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
// 0x558800: the line-of-sight Bresenham, with both ends. A cell with the WORD at 4 or more and no 0x20, 0x40 or 0x80 in
// the low byte blocks, the starting one too. With both differences equal the long axis is Y, and the error moves the
// short one when it passes half of the long one.
TEST(TerrainWallsTest, TheLineOfSightStopsAtTheFirstWall)
{
    Client::Pathfinding::TerrainWalls walls;
    const auto at = [&](int x, int y) -> std::uint16_t& { return walls.cells[static_cast<std::size_t>(y * 256 + x)]; };
    EXPECT_EQ(Client::Pathfinding::CanSeeTheCell(walls.cells, 10, 10, 14, 10), std::optional<bool>{true});
    for (const std::uint16_t word :
         {std::uint16_t{3}, std::uint16_t{0x24}, std::uint16_t{0x44}, std::uint16_t{0x84}, std::uint16_t{0x104}})
    {
        SCOPED_TRACE(word);
        at(12, 10) = word;
        const bool seen = word == 3 || (word & 0xe0) != 0;
        EXPECT_EQ(Client::Pathfinding::CanSeeTheCell(walls.cells, 10, 10, 14, 10), std::optional<bool>{seen});
        EXPECT_EQ(Client::Pathfinding::CanSeeTheCell(walls.cells, 14, 10, 10, 10), std::optional<bool>{seen});
    }
    at(12, 10) = 0;
    at(14, 10) = 4;
    EXPECT_EQ(Client::Pathfinding::CanSeeTheCell(walls.cells, 10, 10, 14, 10), std::optional<bool>{false});
    EXPECT_EQ(Client::Pathfinding::CanSeeTheCell(walls.cells, 14, 10, 10, 10), std::optional<bool>{false});
    at(14, 10) = 0;
    // (10, 10) to (14, 12): X is the long one; the error adds 2 per step and passes 2 on the second and the fourth, so
    // it walks (10, 10), (11, 10), (12, 11), (13, 11) and (14, 12).
    at(12, 10) = 4;
    at(11, 11) = 4;
    EXPECT_EQ(Client::Pathfinding::CanSeeTheCell(walls.cells, 10, 10, 14, 12), std::optional<bool>{true});
    at(12, 11) = 4;
    EXPECT_EQ(Client::Pathfinding::CanSeeTheCell(walls.cells, 10, 10, 14, 12), std::optional<bool>{false});
    // With dx = dy, Y is the long one and the diagonal goes through (11, 11).
    at(12, 11) = 0;
    EXPECT_EQ(Client::Pathfinding::CanSeeTheCell(walls.cells, 10, 10, 13, 13), std::optional<bool>{false});
    // Off the grid: the end point is read from the linear index and falls outside the array.
    EXPECT_EQ(Client::Pathfinding::CanSeeTheCell(walls.cells, 5, 255, 5, 257), std::nullopt);
}

} // namespace
