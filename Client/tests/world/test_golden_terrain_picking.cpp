#include "World/TerrainPicking.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/Pass3D.h"
#include "World/GameCamera.h"
#include "World/TerrainCells.h"
#include "World/TerrainCulling.h"
#include "World/TerrainPickingSegment.h"
#include "World/TerrainPickingState.h"
#include "World/TerrainPickingTarget.h"
#include "World/WorldTerrain.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

// Tests del componente con Data real y Pass3D. Las expectativas son geométricas, no capturas del
// picking nativo. La cámara/héroe de Lorencia proceden de 06 § La cámara del juego y su traza.
namespace
{
using GoldenTerrainPicking = Client::Tests::GoldenDataTest;
using Client::World::PickTerrain;
using Client::World::PickTerrainTarget;
using Client::World::TerrainPickingSegment;
using Client::World::TerrainPickingState;
using Client::World::TerrainPickingTarget;

TEST_F(GoldenTerrainPicking, RealWorldFamiliesUseThePassRayAndTheirCornerHeights)
{
    // Lorencia, Devias, el plano World19 y World56 (byte×3). Cámara cenital: el rayo central toca
    // (1375,1325), interior de 012 en celda(13,13), pesos(1/4,1/2,1/4) independientes del picker.
    constexpr std::array<std::uint8_t, 4> Maps{0, 2, 18, 55};
    for (const std::uint8_t map : Maps)
    {
        SCOPED_TRACE(static_cast<int>(map));
        const auto terrain = Client::World::LoadWorldTerrain(m_dataRoot, map);
        ASSERT_TRUE(terrain.HasValue()) << terrain.Error();
        const Client::World::TerrainView view{.heights = terrain.Value().heights.get(),
                                              .attributes = terrain.Value().attributes.get()};
        const Client::Render::Camera3D camera{.position = {1375.0f, 1325.0f, 1600.0f}, .farPlane = 2000.0f};
        const auto pass = Client::Render::MakePass3D({640, 480}, {0, 0, 640, 432}, camera);
        const auto [origin, end] = pass.RayThrough(320.0f, 216.0f);
        const auto range = Client::World::TerrainRangeOf(pass.view, camera);
        const auto polygon =
            Client::World::TerrainPolygonOf(Client::World::CullingScene::Game, map, {1375.0f, 1325.0f, 0.0f}, 0.0f);
        TerrainPickingState state;
        TerrainPickingTarget target;
        const auto result = PickTerrainTarget(state, target, view, range, polygon, {origin, end});
        ASSERT_TRUE(result.HasValue()) << result.Error();
        ASSERT_TRUE(result.Value());
        EXPECT_EQ(state.selectedX, 13.0f);
        EXPECT_EQ(state.selectedY, 13.0f);
        EXPECT_EQ(target.x, 13u);
        EXPECT_EQ(target.y, 13u);
        EXPECT_EQ(state.hit.x, 1375.0f);
        EXPECT_EQ(state.hit.y, 1325.0f);
        constexpr std::array<std::size_t, 3> Indices{3341, 3342, 3598};
        std::array<double, 3> heights{};
        for (std::size_t i = 0; i < Indices.size(); ++i)
        {
            heights[i] = (view.attributes->cells[Indices[i]] & 0x40U) != 0 ? 1200.0 : view.heights->cells[Indices[i]];
        }
        const double expectedZ = (heights[0] + (2.0 * heights[1]) + heights[2]) / 4.0;
        EXPECT_NEAR(state.hit.z, expectedZ, 0.0002);
        EXPECT_GE(state.maxT, 0.0f);
        EXPECT_LE(state.maxT, 1.0f);
    }
}

TEST_F(GoldenTerrainPicking, LorenciaCameraFeedsTheConsumerAndRetainedSelection)
{
    const auto terrain = Client::World::LoadWorldTerrain(m_dataRoot, 0);
    ASSERT_TRUE(terrain.HasValue()) << terrain.Error();
    Client::World::GameCamera cameraState;
    const Client::World::GameCameraInput input{.hero = {14350.0f, 12050.0f, 165.0f}, .cellAttribute = 0x01};
    const auto camera = Client::World::StepGameCamera(cameraState, input);
    const auto pass = Client::Render::MakePass3D({640, 480}, {0, 0, 640, 432}, camera);
    const auto [origin, end] = pass.RayThrough(323.0f, 217.0f);
    const auto range = Client::World::TerrainRangeOf(pass.view, camera);
    const auto polygon =
        Client::World::TerrainPolygonOf(Client::World::CullingScene::Game, 0, input.hero, camera.angles.z);
    const Client::World::TerrainView view{.heights = terrain.Value().heights.get(),
                                          .attributes = terrain.Value().attributes.get(),
                                          .fixedHeight = cameraState.fixedHeight};
    TerrainPickingState state;
    TerrainPickingTarget target;
    const TerrainPickingSegment segment{origin, end};
    const auto result = PickTerrainTarget(state, target, view, range, polygon, segment);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_TRUE(result.Value());
    EXPECT_GE(state.selectedX, range.firstX);
    EXPECT_LE(state.selectedX, range.lastX + 3);
    EXPECT_GE(state.selectedY, range.firstY);
    EXPECT_LE(state.selectedY, range.lastY + 3);
    const float localX = state.hit.x - (state.selectedX * 100.0f);
    const float localY = state.hit.y - (state.selectedY * 100.0f);
    EXPECT_GT(localX, 0.0f);
    EXPECT_LT(localX, 100.0f);
    EXPECT_GT(localY, 0.0f);
    EXPECT_LT(localY, 100.0f);
    EXPECT_NE(localX, localY);
    const TerrainPickingState selected = state;
    const auto miss = PickTerrain(state, view, range, {}, segment);
    ASSERT_TRUE(miss.HasValue()) << miss.Error();
    EXPECT_FALSE(state.selected);
    EXPECT_EQ(state.hit, selected.hit);
    const auto retained = PickTerrainTarget(state, target, view, range, {}, segment);
    ASSERT_TRUE(retained.HasValue()) << retained.Error();
    ASSERT_TRUE(retained.Value());
    EXPECT_TRUE(state.selected);
    EXPECT_EQ(state.selectedX, selected.selectedX);
    EXPECT_EQ(state.selectedY, selected.selectedY);
    EXPECT_EQ(state.hit, selected.hit);
}
} // namespace
