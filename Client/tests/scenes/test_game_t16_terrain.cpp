#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "World/TerrainCells.h"
#include "World/TerrainCulling.h"
#include "World/TerrainHeight.h"
#include "World/TerrainPicking.h"
#include "World/TerrainPickingSegment.h"
#include "World/TerrainPickingState.h"
#include "World/TerrainPickingTarget.h"
#include "World/WorldTerrain.h"
#include "support/GoldenDir.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace
{
class GameT16TerrainContract : public Client::Tests::GoldenDataTest, public ::testing::WithParamInterface<int>
{
};

// Lector y picking del terreno real: NO afirma que se cargue o camine la escena correspondiente.
// Conserva las filas 5/35 como falta de archivos y permite estudiar 30/34 sin inventar su selector .att.
TEST_P(GameT16TerrainContract, ArchivedCornersFeedPickingAndHeightWithoutClaimingACompleteScene)
{
    const auto map = static_cast<std::uint8_t>(GetParam());
    const auto terrain = Client::World::LoadWorldTerrain(m_dataRoot, map);
    if (map == 5 || map == 35)
    {
        ASSERT_FALSE(terrain.HasValue());
        EXPECT_NE(terrain.Error().find(map == 5 ? "World6" : "World36"), std::string::npos);
        RecordProperty("scope", "missing archive; Q-C180 quit/notice remains separate");
        return;
    }
    ASSERT_TRUE(terrain.HasValue()) << terrain.Error();
    const auto& attributes = *terrain.Value().attributes;
    const auto& heights = *terrain.Value().heights;
    // Interior 012 con pesos 1/4, 1/2, 1/4: un oráculo geométrico independiente del picker.
    constexpr std::uint8_t X = 13;
    constexpr std::uint8_t Y = 13;
    constexpr float HitX = 1375;
    constexpr float HitY = 1325;
    const Client::World::TerrainView view{.heights = &heights, .attributes = &attributes, .map = map};
    Client::World::TerrainPickingState state;
    Client::World::TerrainPickingTarget target;
    const Client::World::TerrainPickingSegment ray{{HitX, HitY, 1600}, {HitX, HitY, -400}};
    const Client::World::TerrainRange range{12, 12, 12, 12};
    const Client::World::TerrainPolygon polygon{.everything = true};
    const auto picked = Client::World::PickTerrainTarget(state, target, view, range, polygon, ray);
    ASSERT_TRUE(picked.HasValue()) << picked.Error();
    ASSERT_TRUE(picked.Value());
    EXPECT_EQ(target.x, X);
    EXPECT_EQ(target.y, Y);
    EXPECT_FLOAT_EQ(state.hit.x, HitX);
    EXPECT_FLOAT_EQ(state.hit.y, HitY);
    const std::array<std::size_t, 3> triangle{Client::Data::Terrain::CellIndex(X, Y),
                                              Client::Data::Terrain::CellIndex(X + 1, Y),
                                              Client::Data::Terrain::CellIndex(X + 1, Y + 1)};
    std::array<double, 3> z{};
    for (std::size_t i = 0; i < triangle.size(); ++i)
        z[i] = (attributes.cells[triangle[i]] & Client::World::FixedHeightBit) != 0 ? view.fixedHeight
                                                                                    : heights.cells[triangle[i]];
    EXPECT_NEAR(state.hit.z, (z[0] + 2 * z[1] + z[2]) / 4, 0.0002);
    // En un vértice, la altura bilineal tiene un oráculo exacto aun si difiere de la triangulación.
    constexpr float InsideVertexBias = 0.01f;
    const float vertexX = static_cast<float>(X) * 100 + InsideVertexBias;
    const float vertexY = static_cast<float>(Y) * 100 + InsideVertexBias;
    EXPECT_NEAR(Client::World::TerrainHeightAt(heights, attributes, vertexX, vertexY), z[0], 0.01);
    RecordProperty("scope", "archived terrain kernel; scene coverage in GameT16SceneMatrix");
}

using GameT16CastleHeight = Client::Tests::GoldenDataTest;
TEST_F(GameT16CastleHeight, TheArchivedCastleBit40UsesTheSharedFixedHeight)
{
    const auto terrain = Client::World::LoadWorldTerrain(m_dataRoot, 30);
    ASSERT_TRUE(terrain.HasValue()) << terrain.Error();
    const auto& attributes = *terrain.Value().attributes;
    const auto& heights = *terrain.Value().heights;
    bool checked = false;
    for (std::size_t cell = 0; cell < attributes.cells.size(); ++cell)
    {
        if ((attributes.cells[cell] & Client::World::FixedHeightBit) == 0)
            continue;
        const float x = static_cast<float>(cell % 256) * 100 + 50;
        const float y = static_cast<float>(cell / 256) * 100 + 50;
        EXPECT_DOUBLE_EQ(Client::World::TerrainHeightAt(heights, attributes, x, y, 1201), 1201);
        checked = true;
    }
    EXPECT_TRUE(checked);
    RecordProperty("scope", "real castle archive height; selector42a760 and complete scene remain boundaries");
}

INSTANTIATE_TEST_SUITE_P(AllMaps0Through36, GameT16TerrainContract, ::testing::Range(0, 37),
                         [](const ::testing::TestParamInfo<int>& mapParameter)
                         { return "Map" + std::to_string(mapParameter.param); });
} // namespace
