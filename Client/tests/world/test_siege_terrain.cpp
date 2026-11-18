#include "World/SiegeTerrain.h"
#include "World/TerrainLighting.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainLight.h"
#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
namespace
{
TEST(SiegeTerrainTest, CraterHasRadialFalloffAndWrapsAtTheNativeByteBoundary)
{
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    heights->cells.fill(100);
    ASSERT_TRUE(Client::World::DeformTerrainHeights(*heights, 0, 0, -30, 2).HasValue());
    EXPECT_EQ(heights->cells[0], 70);
    EXPECT_EQ(heights->cells[1], 85);
    EXPECT_EQ(heights->cells[255], 85);
    EXPECT_EQ(heights->cells[2], 100);
    EXPECT_LT(heights->cells[257], 100);
    EXPECT_GT(heights->cells[257], 85);
}
TEST(SiegeTerrainTest, TheNormalAndLightRefreshClampsAnEightCellWindowAndKeepsOtherCells)
{
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    heights->cells.fill(100);
    auto raw = std::make_unique<Client::Data::Terrain::TerrainLight>();
    raw->cells.fill({.2f, .4f, .6f});
    auto lighting = std::make_unique<Client::World::TerrainLighting>();
    lighting->cells.fill({9, 9, 9});
    Client::World::ReshadeTerrainLightWindow(*lighting, *raw, *heights, -10, -20);
    EXPECT_EQ(lighting->cells[0], raw->cells[0]);
    EXPECT_EQ(lighting->cells[7 + 7 * 256], raw->cells[7 + 7 * 256]);
    EXPECT_EQ(lighting->cells[8 + 7 * 256], (Client::Data::Terrain::TerrainLightColor{9, 9, 9}));
}
} // namespace
