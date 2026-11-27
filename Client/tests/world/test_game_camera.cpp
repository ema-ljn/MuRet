#include "Camera/GameCamera.h"

#include "Render/Pass3D.h"
#include "Terrain/TerrainCulling.h"
#include "support/HexFloatFixture.h"
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
#include <istream>
#include <limits>
#include <sstream>
#include <string>

namespace
{
using Client::Camera::GameCamera;
using Client::Camera::GameCameraInput;
using Client::Camera::GameCameraOf;
using Client::Camera::GameCameraTour;
using Client::Camera::StepGameCamera;
using Client::Terrain::CullingScene;

GameCameraInput Lorencia()
{
    return {{14350.0f, 12050.0f, 165.0f}, 0x01, 0, CullingScene::Game, 0.0f, 0.0f, false, {}, {}};
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

// Pins Lorencia's trace and catches a missing −150 or the wrong sign of the turn.
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

TEST(GameCameraTest, ANonZeroEffectZSelectsTheGlobalTarget)
{
    GameCamera state;
    auto input = Lorencia();
    input.effectTarget = {0.0f, 0.0f, 1.0f};
    const auto camera = StepGameCamera(state, input);
    ExpectBits(camera.position.x, 0x4430c6d5u);
    ExpectBits(camera.position.y, 0xc430c6d5u);
    EXPECT_EQ(camera.position.z, 1015.0f);
    ExpectSameCamera(GameCameraOf(state, input), camera);
}

TEST(GameCameraTest, NegativeZeroInTheEffectKeepsTheHeroTarget)
{
    GameCamera referenceState;
    const auto expected = StepGameCamera(referenceState, Lorencia());
    GameCamera state;
    auto input = Lorencia();
    input.effectTarget = {-0.0f, -0.0f, -0.0f};
    const auto camera = StepGameCamera(state, input);
    ExpectSameCamera(camera, expected);
    ExpectSameCamera(GameCameraOf(state, input), expected);
}

// 0x44dc60 tests C3 without C2: unordered keeps the hero with invalid masked.
TEST(GameCameraTest, AnUnorderedEffectComponentAloneKeepsTheHeroTarget)
{
    constexpr float NaN = std::numeric_limits<float>::quiet_NaN();
    constexpr std::array<glm::vec3, 3> Effects{{{NaN, 0.0f, 0.0f}, {0.0f, NaN, 0.0f}, {0.0f, 0.0f, NaN}}};
    GameCamera referenceState;
    const auto expected = StepGameCamera(referenceState, Lorencia());
    for (const auto& effect : Effects)
    {
        GameCamera state;
        auto input = Lorencia();
        input.effectTarget = effect;
        const auto camera = StepGameCamera(state, input);
        ExpectSameCamera(camera, expected);
        ExpectSameCamera(GameCameraOf(state, input), expected);
    }
}

TEST(GameCameraTest, AnOrderedNonZeroEffectStillSelectsTheGlobalTargetWithNaNZ)
{
    GameCamera state;
    auto input = Lorencia();
    input.effectTarget = {0.0f, 125.0f, std::numeric_limits<float>::quiet_NaN()};
    const auto camera = StepGameCamera(state, input);
    ExpectBits(camera.position.x, 0x4430c6d5u);
    ExpectBits(camera.position.y, 0xc41186d5u);
    EXPECT_EQ(camera.position.z, 1015.0f);
    ExpectSameCamera(GameCameraOf(state, input), camera);
}

TEST(GameCameraTest, GameCameraOfBeforeTheFirstStepKeepsTheInitialState)
{
    GameCamera state;
    state.zoom = 200.0f;
    state.farPlane = 4321.0f;
    auto input = Lorencia();
    input.cellAttribute = 0x80;
    const auto camera = GameCameraOf(state, input);
    // Shift with D1000 and zoom200, without the first +10 or a recomputation of the far.
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
    // The hero moved in a logic step with no original frame: it keeps the drawing's D, zoom and far.
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

// The camera tour of 0x4035f0's script (CL9 milestone 11): at row 0xe8 of column 0x71, the exit of 0x403e00's first
// leg, at 0x403d90's height (the hero's in stage 0, another here to show that it does not count).
constexpr GameCameraTour TourAtTheFirstCell{{11350.0f, 23250.0f, 777.0f}, 2};

// A third of D's way to the target, as 0x63623b..0x63624d (0x6ba2a0's float 1/3).
float SmoothedDistance(float distance, float target)
{
    const double third = std::bit_cast<float>(std::uint32_t{0x3eaaaaab});
    return static_cast<float>(((static_cast<double>(target) - static_cast<double>(distance)) * third) +
                              static_cast<double>(distance));
}

// 0x635ead..0x635efc: with the tour running, the camera looks at its position instead of the hero and 0x44dc60's
// offset, with the hero's base height at 300 (0x635ec0), as if the hero stood there; the tour's z does not count. The
// level stays in its +0x150, which picks D's target at the end of the same frame (0x6361d0: 1,200 with 2) and the next
// frame's far (0x635e00). Without the tour the camera goes back to the hero, with the offset, from the D it had
// reached, which drops toward 1,000. In Lorencia, Devias and Crywolf.
TEST(GameCameraTest, TheTourMovesTheCameraAsIfTheHeroStoodThereAt300)
{
    for (const std::uint8_t map : std::array<std::uint8_t, 3>{0, 2, 34})
    {
        SCOPED_TRACE(static_cast<int>(map));
        GameCamera state;
        auto input = Lorencia();
        input.map = map;
        input.effectTarget = {100.0f, 200.0f, 999.0f};
        input.tour = TourAtTheFirstCell;
        GameCamera twin;
        auto standing = Lorencia();
        standing.map = map;
        standing.hero = {TourAtTheFirstCell.position.x, TourAtTheFirstCell.position.y, 300.0f};
        const auto camera = StepGameCamera(state, input);
        ExpectSameCamera(camera, StepGameCamera(twin, standing));
        EXPECT_EQ(camera.position.z, 1150.0f);
        EXPECT_EQ(state.level, 2);
        EXPECT_EQ(state.targetDistance, 1200.0f);
        EXPECT_EQ(state.distance, SmoothedDistance(1000.0f, 1200.0f));
        EXPECT_EQ(twin.distance, 1000.0f);
        EXPECT_EQ(Client::Camera::FollowedPointOf(input), TourAtTheFirstCell.position);
        EXPECT_EQ(Client::Camera::CameraTargetOf(input), input.effectTarget);
        // The next frame reads level 2 (2,600), leaves it again and moves away with the D that had already risen.
        const float second = state.distance;
        const auto away = StepGameCamera(state, input);
        EXPECT_EQ(away.farPlane, 2600.0f);
        EXPECT_EQ(away.position.z, static_cast<float>((static_cast<double>(second) - 150.0) + 300.0));
        EXPECT_EQ(state.distance, SmoothedDistance(second, 1200.0f));
        // Without the tour, the far reads 2 once more, the target goes back to 1,000 and the camera looks at the
        // offset, with the previous frame's D.
        const float third = state.distance;
        input.tour.reset();
        const auto back = StepGameCamera(state, input);
        EXPECT_EQ(back.farPlane, 2600.0f);
        EXPECT_EQ(state.level, 0);
        EXPECT_EQ(state.targetDistance, 1000.0f);
        EXPECT_EQ(state.distance, SmoothedDistance(third, 1000.0f));
        EXPECT_FLOAT_EQ(back.position.x, 100.0f + (0.70710677f * third));
        EXPECT_FLOAT_EQ(back.position.y, 200.0f - (0.70710677f * third));
        EXPECT_EQ(back.position.z, static_cast<float>((static_cast<double>(third) - 150.0) + 165.0));
        EXPECT_EQ(StepGameCamera(state, input).farPlane, 2000.0f);
    }
}

// 0x6361d0..0x63624d: D's target is 1,000 + 100 · the level's word, which with the tour is its +0x150 (0 to 4, from
// 0x403d40); outside 0..4, the JA of 0x6361da leaves without touching D or the target. Maps 5, 6 with "webzen" and 30
// leave before and do not look at it.
TEST(GameCameraTest, TheTourLevelPicksTheDistanceTarget)
{
    for (std::int32_t level = 0; level <= 4; ++level)
    {
        SCOPED_TRACE(level);
        GameCamera state;
        auto input = Lorencia();
        input.map = 34;
        input.tour = GameCameraTour{TourAtTheFirstCell.position, level};
        static_cast<void>(StepGameCamera(state, input));
        const float target = 1000.0f + (100.0f * static_cast<float>(level));
        EXPECT_EQ(state.targetDistance, target);
        EXPECT_EQ(state.distance, SmoothedDistance(1000.0f, target));
    }
    GameCamera castle;
    auto input = Lorencia();
    input.map = 30;
    input.tour = GameCameraTour{TourAtTheFirstCell.position, 4};
    static_cast<void>(StepGameCamera(castle, input));
    EXPECT_EQ(castle.targetDistance, 1100.0f);
    EXPECT_EQ(castle.distance, 1100.0f);
    GameCamera rocking;
    input.map = 5;
    static_cast<void>(StepGameCamera(rocking, input));
    EXPECT_EQ(rocking.targetDistance, 1000.0f);
    EXPECT_EQ(rocking.distance, 1000.0f);
}

// 0x635ed2 stores AX: the +0x150 stays 16 bits, and 0x635e00 reads it with MOVSX. With 0xffff it is −1, which the JA
// of 0x635e0a leaves outside the far's table and that of 0x6361da outside D's: neither the far, D nor its target
// change.
TEST(GameCameraTest, TheTourLevelIsAWord)
{
    GameCamera state;
    auto input = Lorencia();
    input.tour = GameCameraTour{TourAtTheFirstCell.position, 0x10003};
    static_cast<void>(StepGameCamera(state, input));
    EXPECT_EQ(state.level, 3);
    EXPECT_EQ(state.targetDistance, 1300.0f);
    EXPECT_EQ(StepGameCamera(state, input).farPlane, 2950.0f);
    input.tour->level = 0xffff;
    const float distance = state.distance;
    static_cast<void>(StepGameCamera(state, input));
    EXPECT_EQ(state.level, -1);
    EXPECT_EQ(state.targetDistance, 1300.0f);
    EXPECT_EQ(state.distance, distance);
    EXPECT_EQ(StepGameCamera(state, input).farPlane, 2950.0f);
    EXPECT_EQ(state.distance, distance);
}

// On map 30 the base height is 255 before the hero (0x635f02), also with the tour; on 6 with "webzen" the camera
// leaves before 0x635ead (0x635c4f): neither the tour's position nor its level.
TEST(GameCameraTest, TheTourKeepsTheCastleHeightAndNeverReachesWebzen)
{
    GameCamera castle;
    auto input = Lorencia();
    input.map = 30;
    input.tour = TourAtTheFirstCell;
    EXPECT_EQ(StepGameCamera(castle, input).position.z, 1105.0f);
    GameCamera tour;
    GameCamera plain;
    plain.level = tour.level = 3;
    input.map = 6;
    input.webzen = true;
    const auto camera = StepGameCamera(tour, input);
    input.tour.reset();
    ExpectSameCamera(camera, StepGameCamera(plain, input));
    EXPECT_EQ(tour.level, 3);
}

// An in-between drawing with the tour repeats the last frame's camera, which followed it.
TEST(GameCameraTest, TheRepeatedCameraFollowsTheTour)
{
    GameCamera state;
    auto input = Lorencia();
    input.map = 34;
    input.tour = TourAtTheFirstCell;
    const auto expected = StepGameCamera(state, input);
    ExpectSameCamera(GameCameraOf(state, input), expected);
    input.hero = {1.0f, 2.0f, 3.0f};
    ExpectSameCamera(GameCameraOf(state, input), expected);
}

float NextFloat(std::istringstream& words)
{
    return Client::Tests::ReadHexFloat(words);
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
    std::size_t cases = 0, frames = 0, caseFrames = 0;
    while (std::getline(file, line) && line != "end")
    {
        SCOPED_TRACE(line);
        std::istringstream words(line);
        std::string tag;
        words >> tag;
        if (tag == "case")
        {
            if (cases != 0)
            {
                ASSERT_EQ(caseFrames, 50u);
            }
            unsigned seed = 0, map = 0, scene = 0;
            words >> seed >> map >> scene;
            ASSERT_FALSE(words.fail());
            ASSERT_LE(map, 255u);
            ASSERT_TRUE(scene == 2 || scene == 4 || scene == 5);
            state = {};
            state.distance = NextFloat(words);
            state.zoom = NextFloat(words);
            state.yaw = NextFloat(words);
            words >> state.level;
            state.farPlane = NextFloat(words);
            state.fixedHeight = NextFloat(words);
            state.targetDistance = NextFloat(words);
            ASSERT_FALSE(words.fail());
            ASSERT_TRUE((words >> std::ws).eof());
            input.map = static_cast<std::uint8_t>(map);
            input.scene = static_cast<CullingScene>(scene);
            ++cases;
            caseFrames = 0;
            continue;
        }
        ASSERT_EQ(tag, "frame");
        ASSERT_GT(cases, 0u);
        ASSERT_LT(caseFrames, 50u);
        input.hero = NextVector(words);
        unsigned attr = 0;
        words >> std::hex >> attr >> std::dec;
        ASSERT_FALSE(words.fail());
        ASSERT_LE(attr, 255u);
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
        ASSERT_TRUE((words >> std::ws).eof());
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
        ++caseFrames;
    }
    EXPECT_EQ(line, "end");
    EXPECT_EQ(caseFrames, 50u);
    EXPECT_TRUE((file >> std::ws).eof());
    EXPECT_EQ(cases, 60u);
    EXPECT_EQ(frames, 3000u);
}
} // namespace
