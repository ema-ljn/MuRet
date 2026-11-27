#include "Camera/TerrainPolygonFrame.h"

#include "Kernel/WorldRuntime.h"
#include "Camera/CameraState.h"
#include "Camera/GameCamera.h"
#include "Terrain/TerrainCulling.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <string>

// Client::Camera::StepTheTerrainPolygon and TerrainPolygonBetweenFrames: 0x5fb570's trapezoid (0x78ecbf0 and 0x78ecc00,
// in WorldRuntime) and its effective level (0x7b4ccb8, in the camera's state), one for the three scenes that write
// them. 06 § El trapecio completo.
namespace
{
using Client::Camera::CameraState;
using Client::Camera::GameCamera;
using Client::Camera::StepTheTerrainPolygon;
using Client::Camera::TerrainPolygonBetweenFrames;
using Client::Kernel::WorldRuntime;
using Client::Terrain::TerrainPolygon;
using Client::Terrain::TerrainPolygonInput;

constexpr std::array<float, 4> Zeros{};

// The game's drawing in Lorencia, with the hero at (133, 127) and the starting yaw.
TerrainPolygonInput GameInput()
{
    TerrainPolygonInput input;
    input.scene = static_cast<std::int32_t>(Client::Terrain::CullingScene::Game);
    input.map = 0;
    input.translation = {13350.0f, 12750.0f, 165.0f};
    input.objectPosition = {13350.0f, 12750.0f};
    input.cameraYaw = -45.0f;
    return input;
}

TerrainPolygon PolygonAtLevel(std::int32_t level)
{
    const auto polygon = Client::Terrain::TerrainPolygonOf(GameInput(), level);
    EXPECT_TRUE(polygon.HasValue());
    return polygon.HasValue() ? polygon.Value() : TerrainPolygon{};
}

// The camera that asks for level 0 with D at `distance` and the target at `target`.
GameCamera CameraAt(float distance, float target)
{
    GameCamera camera;
    camera.level = 0;
    camera.distance = distance;
    camera.targetDistance = target;
    return camera;
}

// Q-C217: what WinMain finds in the .bss, the trapezoid's eight floats at zero and the level at 0.
TEST(TerrainPolygonFrameTest, TheGlobalsStartWithTheZerosOfTheBss)
{
    const WorldRuntime runtime;
    const CameraState camera;
    EXPECT_EQ(runtime.terrainPolygon.x, Zeros);
    EXPECT_EQ(runtime.terrainPolygon.y, Zeros);
    EXPECT_FALSE(runtime.terrainPolygon.everything);
    EXPECT_EQ(camera.terrainPolygon.effectiveLevel, 0);
}

// 0x5fb72a..0x5fb731 and 0x5fba55..0x5fba68: with the target at D, the requested level (0x7d3a0ec) becomes the
// effective one and that level's trapezoid stays in the globals.
TEST(TerrainPolygonFrameTest, AFrameLeavesTheLevelAndTheTrapezoid)
{
    ASSERT_NE(PolygonAtLevel(0).x, PolygonAtLevel(3).x);
    CameraState state;
    TerrainPolygon trapezoid;
    state.terrainPolygon.effectiveLevel = 3;

    const auto polygon = StepTheTerrainPolygon(state, trapezoid, GameInput(), CameraAt(1000.0f, 1000.0f));

    ASSERT_TRUE(polygon.HasValue()) << polygon.Error();
    const TerrainPolygon expected = PolygonAtLevel(0);
    EXPECT_EQ(polygon.Value().x, expected.x);
    EXPECT_EQ(polygon.Value().y, expected.y);
    EXPECT_EQ(state.terrainPolygon.effectiveLevel, 0);
    EXPECT_EQ(trapezoid.x, expected.x);
    EXPECT_EQ(trapezoid.y, expected.y);
}

// 0x5fb70e..0x5fb738: with the target (0x6d5ee0) below D (0x7d36ba4), the camera approaching, the effective level
// does not change (0x5fb728's jl) and the trapezoid is the previous level's.
TEST(TerrainPolygonFrameTest, WhileTheCameraApproachesTheLevelStays)
{
    CameraState state;
    TerrainPolygon trapezoid;
    state.terrainPolygon.effectiveLevel = 3;

    const auto polygon = StepTheTerrainPolygon(state, trapezoid, GameInput(), CameraAt(1200.0f, 1000.0f));

    ASSERT_TRUE(polygon.HasValue()) << polygon.Error();
    const TerrainPolygon expected = PolygonAtLevel(3);
    EXPECT_EQ(state.terrainPolygon.effectiveLevel, 3);
    EXPECT_EQ(trapezoid.x, expected.x);
    EXPECT_EQ(trapezoid.y, expected.y);
}

// ADR 0009: with D outside an int64 it leaves nothing and the error names 0x5fb570.
TEST(TerrainPolygonFrameTest, AnErrorLeavesTheGlobalsAsTheyWere)
{
    CameraState state;
    TerrainPolygon trapezoid;
    state.terrainPolygon.effectiveLevel = 2;
    trapezoid.x = {1.0f, 2.0f, 3.0f, 4.0f};

    const auto polygon = StepTheTerrainPolygon(state, trapezoid, GameInput(),
                                               CameraAt(std::numeric_limits<float>::quiet_NaN(), 1000.0f));

    ASSERT_FALSE(polygon.HasValue());
    EXPECT_NE(polygon.Error().find("0x5fb570"), std::string::npos) << polygon.Error();
    EXPECT_EQ(state.terrainPolygon.effectiveLevel, 2);
    EXPECT_EQ(trapezoid.x, (std::array<float, 4>{1.0f, 2.0f, 3.0f, 4.0f}));
    EXPECT_EQ(trapezoid.y, Zeros);
}

// A MuRet in-between drawing: the trapezoid with the current effective level, without the transition and without
// writing. A level outside 0 to 4 gives 0x5fb570's error.
TEST(TerrainPolygonFrameTest, BetweenFramesNothingIsWritten)
{
    CameraState state;
    state.terrainPolygon.effectiveLevel = 3;

    const auto polygon = TerrainPolygonBetweenFrames(state, GameInput());

    ASSERT_TRUE(polygon.HasValue()) << polygon.Error();
    EXPECT_EQ(polygon.Value().x, PolygonAtLevel(3).x);
    EXPECT_EQ(state.terrainPolygon.effectiveLevel, 3);

    state.terrainPolygon.effectiveLevel = 5;
    const auto invalid = TerrainPolygonBetweenFrames(state, GameInput());
    ASSERT_FALSE(invalid.HasValue());
    EXPECT_NE(invalid.Error().find("0x5fb570"), std::string::npos) << invalid.Error();
}
} // namespace
