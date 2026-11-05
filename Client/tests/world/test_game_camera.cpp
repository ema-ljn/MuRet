#include "World/GameCamera.h"

#include "Render/Pass3D.h"
#include "World/TerrainCulling.h"
#include "support/WorldObjectVectors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <sstream>
#include <string>

namespace
{
using Client::World::CullingScene;
using Client::World::GameCamera;
using Client::World::GameCameraInput;
using Client::World::GameCameraOf;
using Client::World::StepGameCamera;

GameCameraInput Lorencia()
{
    return {{14350.0f, 12050.0f, 165.0f}, 0x01, 0, CullingScene::Game, 0.0f, 0.0f, false, {}};
}

void ExpectBits(float actual, std::uint32_t expected)
{
    EXPECT_EQ(std::bit_cast<std::uint32_t>(actual), expected);
}

void ExpectSameCamera(const Client::Render::Camera3D& actual, const Client::Render::Camera3D& expected)
{
    for (int k = 0; k < 3; ++k)
    {
        ExpectBits(actual.position[k], std::bit_cast<std::uint32_t>(expected.position[k]));
        ExpectBits(actual.angles[k], std::bit_cast<std::uint32_t>(expected.angles[k]));
    }
    ExpectBits(actual.fieldOfView, std::bit_cast<std::uint32_t>(expected.fieldOfView));
    ExpectBits(actual.nearPlane, std::bit_cast<std::uint32_t>(expected.nearPlane));
    ExpectBits(actual.farPlane, std::bit_cast<std::uint32_t>(expected.farPlane));
    EXPECT_EQ(actual.skipXRotation, expected.skipXRotation);
}

// Fija la traza de Lorencia y detecta la falta de −150 o el signo incorrecto del giro.
TEST(GameCameraTest, TheCameraOfLorenciaIsTheOneInTheTrace)
{
    GameCamera state;
    const auto camera = StepGameCamera(state, Lorencia());
    ExpectBits(camera.position.x, 0x466b446du);
    ExpectBits(camera.position.y, 0x46313b93u);
    ExpectBits(camera.position.z, 0x447dc000u);
    EXPECT_EQ(camera.angles, (glm::vec3{-48.5f, 0.0f, -45.0f}));
    EXPECT_EQ(camera.farPlane, 2000.0f);
    EXPECT_EQ(camera.fieldOfView, 35.0f);
    EXPECT_EQ(camera.nearPlane, 20.0f);
    EXPECT_FALSE(camera.skipXRotation);
}

TEST(GameCameraTest, TheZoomGoesTo210InTwentyOneFramesAndBack)
{
    GameCamera state;
    auto input = Lorencia();
    input.cellAttribute = 0x80;
    Client::Render::Camera3D camera;
    for (int frame = 1; frame <= 22; ++frame)
    {
        camera = StepGameCamera(state, input);
        EXPECT_EQ(state.zoom, static_cast<float>((frame <= 21 ? frame : 21) * 10));
    }
    EXPECT_LT(camera.position.x, 15057.0f);
    EXPECT_GT(camera.position.y, 11343.0f);
    input.cellAttribute = 0;
    for (int frame = 1; frame <= 21; ++frame)
    {
        camera = StepGameCamera(state, input);
        EXPECT_EQ(state.zoom, static_cast<float>((21 - frame) * 10));
    }
    ExpectBits(camera.position.x, 0x466b446du);
    ExpectBits(camera.position.y, 0x46313b93u);
}

TEST(GameCameraTest, TheBit0x40LeavesTheFixedHeightAt1201)
{
    GameCamera state;
    auto input = Lorencia();
    input.cellAttribute = 0x40;
    EXPECT_EQ(StepGameCamera(state, input).position.z, 2051.0f);
    EXPECT_EQ(state.fixedHeight, 1201.0f);
    input.cellAttribute = 0;
    EXPECT_EQ(StepGameCamera(state, input).position.z, 1015.0f);
    EXPECT_EQ(state.fixedHeight, 1201.0f);
}

TEST(GameCameraTest, TheFarIs3000InsideTheRectangleOnAnyMap)
{
    struct Border
    {
        float x;
        float y;
        bool inside;
    };
    constexpr std::array<Border, 12> Borders{{{16100.0f, 20000.0f, true},
                                              {16099.99f, 20000.0f, false},
                                              {16100.01f, 20000.0f, true},
                                              {19000.0f, 20000.0f, true},
                                              {18999.99f, 20000.0f, true},
                                              {19000.01f, 20000.0f, false},
                                              {17500.0f, 18900.0f, true},
                                              {17500.0f, 18899.99f, false},
                                              {17500.0f, 18900.01f, true},
                                              {17500.0f, 21700.0f, true},
                                              {17500.0f, 21699.99f, true},
                                              {17500.0f, 21700.01f, false}}};
    for (const std::uint8_t map : std::array<std::uint8_t, 2>{0, 3})
    {
        for (const auto& border : Borders)
        {
            GameCamera state;
            auto input = Lorencia();
            input.map = map;
            input.hero.x = border.x;
            input.hero.y = border.y;
            EXPECT_EQ(StepGameCamera(state, input).farPlane, border.inside ? 3000.0f : 2000.0f)
                << static_cast<int>(map) << ":" << border.x << ":" << border.y;
        }
    }
}

TEST(GameCameraTest, TheFarUsesTheLevelOfTheFrameBefore)
{
    constexpr std::array<float, 5> Far{2000.0f, 2500.0f, 2600.0f, 2950.0f, 3200.0f};
    for (int level = 0; level <= 4; ++level)
    {
        GameCamera state;
        state.level = level;
        EXPECT_EQ(StepGameCamera(state, Lorencia()).farPlane, Far[static_cast<std::size_t>(level)]);
        EXPECT_EQ(state.level, 0);
        EXPECT_EQ(StepGameCamera(state, Lorencia()).farPlane, 2000.0f);
    }
    for (const int level : {-1, 5})
    {
        GameCamera state;
        state.level = level;
        state.farPlane = 4321.0f;
        EXPECT_EQ(StepGameCamera(state, Lorencia()).farPlane, 4321.0f);
        EXPECT_EQ(state.level, 0);
    }
}

TEST(GameCameraTest, TheDistanceSettlesAt0x447a0001)
{
    GameCamera state;
    state.distance = 1400.0f;
    EXPECT_EQ(StepGameCamera(state, Lorencia()).position.z, 1415.0f);
    for (int frame = 1; frame < 80; ++frame)
        static_cast<void>(StepGameCamera(state, Lorencia()));
    ExpectBits(state.distance, 0x447a0001u);
    EXPECT_EQ(state.targetDistance, 1000.0f);
}

TEST(GameCameraTest, TheShakeAddsToThePitch)
{
    GameCamera state;
    auto input = Lorencia();
    input.shake = 0.7f;
    EXPECT_EQ(StepGameCamera(state, input).angles.x, -47.8f);
}

TEST(GameCameraTest, TheEffectTargetMovesOnlyTheHorizontalPosition)
{
    GameCamera state;
    auto input = Lorencia();
    input.effectTarget = {100.0f, 200.0f, 999.0f};
    const auto camera = StepGameCamera(state, input);
    EXPECT_FLOAT_EQ(camera.position.x, 807.10675f);
    EXPECT_FLOAT_EQ(camera.position.y, -507.10675f);
    EXPECT_EQ(camera.position.z, 1015.0f);
}

TEST(GameCameraTest, GameCameraOfBeforeTheFirstStepKeepsTheInitialState)
{
    GameCamera state;
    state.zoom = 200.0f;
    state.farPlane = 4321.0f;
    auto input = Lorencia();
    input.cellAttribute = 0x80;
    const auto camera = GameCameraOf(state, input);
    // Corrimiento con D1000 y zoom200, sin el primer +10 ni un recálculo del far.
    ExpectBits(camera.position.x, 0x46690ebeu);
    ExpectBits(camera.position.y, 0x46337142u);
    EXPECT_EQ(camera.position.z, 1015.0f);
    EXPECT_EQ(camera.farPlane, 4321.0f);
    EXPECT_EQ(state.zoom, 200.0f);
    EXPECT_EQ(state.distance, 1000.0f);
    EXPECT_EQ(state.farPlane, 4321.0f);
    EXPECT_FALSE(state.lastFrameDistance);
}

TEST(GameCameraTest, GameCameraOfRepeatsTheLastStep)
{
    GameCamera state;
    state.distance = 1400.0f;
    auto input = Lorencia();
    input.cellAttribute = 0x80;
    auto expected = StepGameCamera(state, input);
    ExpectSameCamera(GameCameraOf(state, input), expected);
    const float distance = state.distance;
    // El héroe avanzó en un paso lógico sin frame original: conserva D, zoom y far del dibujo.
    input.hero += glm::vec3{128.0f, 256.0f, 32.0f};
    input.cellAttribute = 0;
    expected.position += glm::vec3{128.0f, 256.0f, 32.0f};
    ExpectSameCamera(GameCameraOf(state, input), expected);
    ExpectSameCamera(GameCameraOf(state, input), expected);
    EXPECT_EQ(state.distance, distance);
    EXPECT_EQ(state.zoom, 10.0f);
    EXPECT_EQ(state.fixedHeight, 1200.0f);
}

TEST(GameCameraTest, TheRepeatedCameraKeepsTheFixedScenesAndWebzen)
{
    for (const auto scene : {CullingScene::Login, CullingScene::CharacterSelect, CullingScene::Game})
    {
        GameCamera state;
        state.distance = 1400.0f;
        auto input = Lorencia();
        input.scene = scene;
        input.map = scene == CullingScene::Game ? 6 : 0;
        input.webzen = scene == CullingScene::Game;
        input.cellAttribute = 0x80;
        const auto expected = StepGameCamera(state, input);
        input.hero = {1.0f, 2.0f, 3.0f};
        ExpectSameCamera(GameCameraOf(state, input), expected);
        EXPECT_EQ(state.zoom, input.webzen ? 0.0f : 10.0f);
    }
}

TEST(GameCameraTest, TheRepeatedCameraKeepsTheFarOfThePreviousFrame)
{
    GameCamera state;
    auto input = Lorencia();
    static_cast<void>(StepGameCamera(state, input));
    input.hero = {17000.0f, 20000.0f, 165.0f};
    EXPECT_EQ(GameCameraOf(state, input).farPlane, 2000.0f);
    input.map = 6;
    input.webzen = true;
    EXPECT_EQ(GameCameraOf(state, input).farPlane, 2000.0f);
}

TEST(GameCameraTest, TheRepeatedCameraReadsTheCurrentEffectAndHeight)
{
    GameCamera state;
    auto input = Lorencia();
    static_cast<void>(StepGameCamera(state, input));
    input.effectTarget = {100.0f, 200.0f, 999.0f};
    auto camera = GameCameraOf(state, input);
    EXPECT_FLOAT_EQ(camera.position.x, 807.10675f);
    EXPECT_FLOAT_EQ(camera.position.y, -507.10675f);
    EXPECT_EQ(camera.position.z, 1015.0f);
    input.cellAttribute = 0x40;
    camera = GameCameraOf(state, input);
    EXPECT_EQ(camera.position.z, 2051.0f);
    EXPECT_EQ(state.fixedHeight, 1200.0f);
}

float NextFloat(std::istringstream& words)
{
    std::string word;
    words >> word;
    return std::bit_cast<float>(static_cast<std::uint32_t>(std::stoul(word, nullptr, 16)));
}

glm::vec3 NextVector(std::istringstream& words)
{
    const float x = NextFloat(words);
    const float y = NextFloat(words);
    const float z = NextFloat(words);
    return {x, y, z};
}

TEST(GameCameraVectorsTest, EachFrameMatchesTheRealCode)
{
    std::ifstream file(Client::Tests::World55VectorsDir().parent_path() / "game" / "camera.txt");
    ASSERT_TRUE(file);
    std::string line;
    ASSERT_TRUE(std::getline(file, line));
    ASSERT_EQ(line, "camera 2");
    GameCamera state;
    GameCameraInput input{};
    std::size_t cases = 0, frames = 0;
    while (std::getline(file, line) && line != "end")
    {
        SCOPED_TRACE(line);
        std::istringstream words(line);
        std::string tag;
        words >> tag;
        if (tag == "case")
        {
            unsigned seed = 0, map = 0, scene = 0;
            words >> seed >> map >> scene;
            state = {};
            state.distance = NextFloat(words);
            state.zoom = NextFloat(words);
            state.yaw = NextFloat(words);
            words >> state.level;
            state.farPlane = NextFloat(words);
            state.fixedHeight = NextFloat(words);
            state.targetDistance = NextFloat(words);
            input.map = static_cast<std::uint8_t>(map);
            input.scene = static_cast<CullingScene>(scene);
            ++cases;
            continue;
        }
        ASSERT_EQ(tag, "frame");
        input.hero = NextVector(words);
        unsigned attr = 0;
        words >> std::hex >> attr >> std::dec;
        input.cellAttribute = static_cast<std::uint8_t>(attr);
        input.shake = NextFloat(words);
        input.worldTime = NextFloat(words);
        words >> input.webzen;
        input.effectTarget = NextVector(words);
        Client::Render::Camera3D expected;
        expected.position = NextVector(words);
        expected.angles = NextVector(words);
        const float zoom = NextFloat(words), distance = NextFloat(words), target = NextFloat(words);
        int level = 0;
        words >> level;
        const float far = NextFloat(words), height = NextFloat(words);
        expected.farPlane = far;
        expected.fieldOfView = NextFloat(words);
        expected.nearPlane = NextFloat(words);
        ASSERT_FALSE(words.fail());
        const float frameDistance = state.distance;
        const auto actual = StepGameCamera(state, input);
        ASSERT_TRUE(state.lastFrameDistance);
        ExpectBits(*state.lastFrameDistance, std::bit_cast<std::uint32_t>(frameDistance));
        ExpectSameCamera(actual, expected);
        ExpectSameCamera(GameCameraOf(state, input), expected);
        ExpectBits(state.yaw, std::bit_cast<std::uint32_t>(expected.angles.z));
        ExpectBits(state.zoom, std::bit_cast<std::uint32_t>(zoom));
        ExpectBits(state.distance, std::bit_cast<std::uint32_t>(distance));
        ExpectBits(state.targetDistance, std::bit_cast<std::uint32_t>(target));
        EXPECT_EQ(state.level, level);
        ExpectBits(state.farPlane, std::bit_cast<std::uint32_t>(far));
        ExpectBits(state.fixedHeight, std::bit_cast<std::uint32_t>(height));
        ++frames;
    }
    EXPECT_EQ(line, "end");
    EXPECT_EQ(cases, 60u);
    EXPECT_EQ(frames, 3000u);
}
} // namespace
