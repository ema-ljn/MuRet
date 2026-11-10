#include "Scenes/TerrainPolygonFrame.h"

#include "Scenes/WorldRuntime.h"
#include "World/GameCamera.h"
#include "World/TerrainCulling.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <string>

// Client::Scenes::StepTheTerrainPolygon y TerrainPolygonBetweenFrames: el trapecio de 0x5fb570 y su nivel
// efectivo en los globales de WorldRuntime (0x78ecbf0, 0x78ecc00 y 0x7b4ccb8), uno para las tres escenas que los
// escriben. 06 § El trapecio completo.
namespace
{
using Client::Scenes::StepTheTerrainPolygon;
using Client::Scenes::TerrainPolygonBetweenFrames;
using Client::Scenes::WorldRuntime;
using Client::World::GameCamera;
using Client::World::TerrainPolygon;
using Client::World::TerrainPolygonInput;

constexpr std::array<float, 4> Zeros{};

// El dibujo del juego en Lorencia, con el héroe en (133, 127) y el yaw de arranque.
TerrainPolygonInput GameInput()
{
    TerrainPolygonInput input;
    input.scene = static_cast<std::int32_t>(Client::World::CullingScene::Game);
    input.map = 0;
    input.translation = {13350.0f, 12750.0f, 165.0f};
    input.objectPosition = {13350.0f, 12750.0f};
    input.cameraYaw = -45.0f;
    return input;
}

TerrainPolygon PolygonAtLevel(std::int32_t level)
{
    const auto polygon = Client::World::TerrainPolygonOf(GameInput(), level);
    EXPECT_TRUE(polygon.HasValue());
    return polygon.HasValue() ? polygon.Value() : TerrainPolygon{};
}

// La cámara que pide el nivel 0 con la D en `distance` y la meta en `target`.
GameCamera CameraAt(float distance, float target)
{
    GameCamera camera;
    camera.level = 0;
    camera.distance = distance;
    camera.targetDistance = target;
    return camera;
}

// Q-C217: lo que WinMain encuentra en la .bss, los ocho float del trapecio en cero y el nivel en 0.
TEST(TerrainPolygonFrameTest, TheRuntimeStartsWithTheZerosOfTheBss)
{
    const WorldRuntime runtime;
    EXPECT_EQ(runtime.terrainPolygon.x, Zeros);
    EXPECT_EQ(runtime.terrainPolygon.y, Zeros);
    EXPECT_FALSE(runtime.terrainPolygon.everything);
    EXPECT_EQ(runtime.terrainPolygonState.effectiveLevel, 0);
}

// 0x5fb72a..0x5fb731 y 0x5fba55..0x5fba68: con la meta a la altura de la D, el nivel pedido (0x7d3a0ec) pasa a ser
// el efectivo y el trapecio de ese nivel queda en los globales.
TEST(TerrainPolygonFrameTest, AFrameLeavesTheLevelAndTheTrapezoid)
{
    ASSERT_NE(PolygonAtLevel(0).x, PolygonAtLevel(3).x);
    WorldRuntime runtime;
    runtime.terrainPolygonState.effectiveLevel = 3;

    const auto polygon = StepTheTerrainPolygon(runtime, GameInput(), CameraAt(1000.0f, 1000.0f));

    ASSERT_TRUE(polygon.HasValue()) << polygon.Error();
    const TerrainPolygon expected = PolygonAtLevel(0);
    EXPECT_EQ(polygon.Value().x, expected.x);
    EXPECT_EQ(polygon.Value().y, expected.y);
    EXPECT_EQ(runtime.terrainPolygonState.effectiveLevel, 0);
    EXPECT_EQ(runtime.terrainPolygon.x, expected.x);
    EXPECT_EQ(runtime.terrainPolygon.y, expected.y);
}

// 0x5fb70e..0x5fb738: con la meta (0x6d5ee0) debajo de la D (0x7d36ba4), la cámara que se acerca, el nivel efectivo
// no cambia (el jl de 0x5fb728) y el trapecio es el del nivel de antes.
TEST(TerrainPolygonFrameTest, WhileTheCameraApproachesTheLevelStays)
{
    WorldRuntime runtime;
    runtime.terrainPolygonState.effectiveLevel = 3;

    const auto polygon = StepTheTerrainPolygon(runtime, GameInput(), CameraAt(1200.0f, 1000.0f));

    ASSERT_TRUE(polygon.HasValue()) << polygon.Error();
    const TerrainPolygon expected = PolygonAtLevel(3);
    EXPECT_EQ(runtime.terrainPolygonState.effectiveLevel, 3);
    EXPECT_EQ(runtime.terrainPolygon.x, expected.x);
    EXPECT_EQ(runtime.terrainPolygon.y, expected.y);
}

// ADR 0009: con la D fuera de un int64 no deja nada y el error nombra 0x5fb570.
TEST(TerrainPolygonFrameTest, AnErrorLeavesTheGlobalsAsTheyWere)
{
    WorldRuntime runtime;
    runtime.terrainPolygonState.effectiveLevel = 2;
    runtime.terrainPolygon.x = {1.0f, 2.0f, 3.0f, 4.0f};

    const auto polygon =
        StepTheTerrainPolygon(runtime, GameInput(), CameraAt(std::numeric_limits<float>::quiet_NaN(), 1000.0f));

    ASSERT_FALSE(polygon.HasValue());
    EXPECT_NE(polygon.Error().find("0x5fb570"), std::string::npos) << polygon.Error();
    EXPECT_EQ(runtime.terrainPolygonState.effectiveLevel, 2);
    EXPECT_EQ(runtime.terrainPolygon.x, (std::array<float, 4>{1.0f, 2.0f, 3.0f, 4.0f}));
    EXPECT_EQ(runtime.terrainPolygon.y, Zeros);
}

// Un dibujo intermedio de MuRet: el trapecio con el nivel efectivo que hay, sin la transición y sin escribir. Un
// nivel fuera de 0 a 4 da el error de 0x5fb570.
TEST(TerrainPolygonFrameTest, BetweenFramesNothingIsWritten)
{
    WorldRuntime runtime;
    runtime.terrainPolygonState.effectiveLevel = 3;

    const auto polygon = TerrainPolygonBetweenFrames(runtime, GameInput());

    ASSERT_TRUE(polygon.HasValue()) << polygon.Error();
    EXPECT_EQ(polygon.Value().x, PolygonAtLevel(3).x);
    EXPECT_EQ(runtime.terrainPolygon.x, Zeros);
    EXPECT_EQ(runtime.terrainPolygonState.effectiveLevel, 3);

    runtime.terrainPolygonState.effectiveLevel = 5;
    const auto invalid = TerrainPolygonBetweenFrames(runtime, GameInput());
    ASSERT_FALSE(invalid.HasValue());
    EXPECT_NE(invalid.Error().find("0x5fb570"), std::string::npos) << invalid.Error();
}
} // namespace
