#include "Core/Expected.h"
#include "Scenes/FaunaStep.h"

#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Render/Models/Cloth.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaObject.h"
#include "Scenes/FaunaMotion.h"
#include "Data/Models/Model.h"
#include "Render/TextureSlot.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/FaunaState.h"
#include "World/IBirdStepServices.h"
#include "World/IFishStepServices.h"
#include "World/TerrainLighting.h"
#include "World/WorldTerrain.h"

#include <gtest/gtest.h>

#include <memory>
#include <cstdint>
#include <cstddef>
#include <system_error>
#include <glm/ext/vector_float3.hpp>
#include <span>

namespace
{
class Models : public Client::Scenes::IModelLibrary
{
public:
    Client::Render::Models::Skeleton skeleton;
    bool missingPrefix = false;
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        if (model == 0xffff && missingPrefix)
            return nullptr;
        return &skeleton;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
};
class FaunaStepTest : public ::testing::Test
{
protected:
    Client::Scenes::FaunaState m_fauna;
    std::int32_t m_map = 0, m_storm = 0;
    glm::vec3 m_hero{1000, 1000, 0};
    std::uint8_t m_action = 0, m_flag = 0;
    Client::World::WorldTerrain m_terrain;
    Client::World::TerrainLighting m_light;
    Models m_models;
    Client::Render::Models::ClothWorld m_cloth;
    Client::Scenes::FaunaStepContext Context(float time = 0)
    {
        return {m_fauna, m_map, m_storm, m_hero, m_action, m_flag, m_terrain, m_light, m_models, m_cloth, time};
    }
    void SetUp() override
    {
        m_terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
        m_terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        m_terrain.mapping = std::make_unique<Client::Data::Terrain::TerrainMapping>();
    }
};
// 445b90: 0→1→3, dos if sucesivos y dos sorteos de altura en el mismo paso.
TEST_F(FaunaStepTest, LandingAndTakingOffRunTheirNewStatesInTheSameStep)
{
    auto context = Context();
    auto& bird = m_fauna.Birds()[0];
    bird.position = {1200, 1000, -100};
    bird.scale = 1;
    Client::Render::Models::CrtRandom expected;
    const int first = expected.Next() % 16 - 8;
    const int second = expected.Next() % 16 - 8;
    const auto result = RunFaunaBirdHandler(context, Client::World::BirdHandler::MoveAdBc, bird, 0);
    EXPECT_FALSE(result.error);
    EXPECT_EQ(bird.motionState, 3);
    EXPECT_FLOAT_EQ(bird.vertical, 20);
    EXPECT_FLOAT_EQ(bird.position.z, static_cast<float>(-100 + first + second));
    EXPECT_FLOAT_EQ(bird.scale, static_cast<float>(static_cast<double>(1.1f) - static_cast<double>(0.005f)));
    EXPECT_EQ(m_cloth.random.State(), expected.State());
}
TEST_F(FaunaStepTest, PerchedBirdAvoidsEveryCommonMovementDraw)
{
    auto context = Context();
    auto& bird = m_fauna.Birds()[0];
    bird.motionState = 2;
    bird.position = {1200, 1000, 0};
    bird.type = 0xad;
    bird.scale = 1;
    const auto result = RunFaunaBirdHandler(context, Client::World::BirdHandler::CommonMotion, bird, 0);
    EXPECT_FALSE(result.error);
    EXPECT_EQ(m_cloth.random.State(), 1U);
    EXPECT_EQ(bird.position, glm::vec3(1200, 1000, 0));
}
TEST_F(FaunaStepTest, CommonMovementSeesLivingNeighborsOutsideTheStepPrefix)
{
    auto context = Context();
    auto& bird = m_fauna.Birds()[0];
    bird.active = 1;
    bird.type = 0xad;
    bird.scale = 1;
    bird.turnRate = 13;
    bird.position = m_hero;
    auto& last = m_fauna.Birds()[39];
    last.active = 1;
    last.position = m_hero + glm::vec3(100, 100, 0);
    last.aheadX = last.position.x + 10;
    last.aheadY = last.position.y + 10;
    const auto result = RunFaunaBirdHandler(context, Client::World::BirdHandler::CommonMotion, bird, 0);
    EXPECT_FALSE(result.error);
    EXPECT_FLOAT_EQ(bird.angles.z, 13); // 4e97a0: (360 - trunc(atan(-1)*K24) + 360) % 360.
    EXPECT_NE(bird.position.x, m_hero.x + 25);
}
TEST_F(FaunaStepTest, KalimaGateReadsTheSharedCounterAndSpendsItsCloseDraws)
{
    m_map = 24;
    m_cloth.logicSteps = 99;
    auto context = Context();
    auto& bird = m_fauna.Birds()[0];
    bird.active = 1;
    Client::Render::Models::CrtRandom expected;
    for (int i = 0; i < 4; ++i)
        static_cast<void>(expected.Next());
    const auto result = RunFaunaBirdHandler(context, Client::World::BirdHandler::SpawnKalima, bird, 0);
    EXPECT_FALSE(result.error);
    EXPECT_EQ(result.value, 1);
    EXPECT_EQ(bird.active, 0);
    EXPECT_EQ(m_cloth.random.State(), expected.State());
}
TEST_F(FaunaStepTest, LorenciaBirthAndStepUseTheRealSharedRandomAndTerrain)
{
    auto context = Context();
    const auto result = StepGameFauna(context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    std::size_t alive = 0;
    for (const auto& bird : m_fauna.Birds())
        if (bird.active != 0)
            ++alive;
    EXPECT_EQ(alive, 5U);
    EXPECT_EQ(m_fauna.Birds()[0].type, 0xad);
    EXPECT_EQ(m_fauna.Birds()[0].timer, -1);
    EXPECT_GT(m_fauna.Birds()[0].alpha, 0);
    EXPECT_NE(m_fauna.Birds()[0].aheadX, 0);
    EXPECT_EQ(m_fauna.Fish()[0].active, 0);
}

TEST_F(FaunaStepTest, IcarusSpawnHasSixDrawsAndWritesTheNativeFlightFields)
{
    m_map = 10;
    auto context = Context();
    auto& bird = m_fauna.Birds()[0];
    const auto result = RunFaunaBirdHandler(context, Client::World::BirdHandler::SpawnIcarus, bird, 0);
    ASSERT_FALSE(result.error);
    EXPECT_EQ(result.value, 1);
    EXPECT_EQ(bird.type, 0x171);
    EXPECT_EQ(bird.active, 1);
    EXPECT_FLOAT_EQ(bird.bodyScale, .4f);
    EXPECT_FLOAT_EQ(bird.scale, .34f);
    EXPECT_FLOAT_EQ(bird.turnRate, .7f);
    EXPECT_EQ(bird.position, glm::vec3(2169, 2724, -600));
    EXPECT_FLOAT_EQ(bird.angles.z, 220);
    EXPECT_EQ(bird.action, 7);
    EXPECT_EQ(bird.hiddenMesh, -1);
    EXPECT_EQ(bird.alphaMode, 0);
    Client::Render::Models::CrtRandom expected;
    for (int i = 0; i < 6; ++i)
        static_cast<void>(expected.Next());
    EXPECT_EQ(m_cloth.random.State(), expected.State());
}
TEST_F(FaunaStepTest, AtlansSpawnUsesItsOwnScaleHeightAndUpperWaterGate)
{
    m_map = 7;
    auto context = Context();
    auto& bird = m_fauna.Birds()[0];
    const auto result = RunFaunaBirdHandler(context, Client::World::BirdHandler::SpawnAtlans, bird, 0);
    ASSERT_FALSE(result.error);
    EXPECT_EQ(result.value, 1);
    EXPECT_EQ(bird.type, 0xb4);
    EXPECT_EQ(bird.position, glm::vec3(1388, 1225, 274));
    EXPECT_FLOAT_EQ(bird.scale, .3f);
    EXPECT_FLOAT_EQ(bird.phase, 2.55f);
    EXPECT_EQ(bird.vertexLight, 0);
    const auto before = m_cloth.random.State();
    m_hero.y = 12801;
    bird.active = 1;
    EXPECT_FALSE(RunFaunaBirdHandler(context, Client::World::BirdHandler::SpawnAtlans, bird, 0).error);
    EXPECT_EQ(bird.active, 0);
    EXPECT_EQ(m_cloth.random.State(), before);
}
TEST_F(FaunaStepTest, KalimaBirthClosesCounterBeforeSixSpawnDraws)
{
    m_map = 24;
    m_cloth.logicSteps = 100;
    auto context = Context();
    auto& bird = m_fauna.Birds()[0];
    const auto result = RunFaunaBirdHandler(context, Client::World::BirdHandler::SpawnKalima, bird, 0);
    ASSERT_FALSE(result.error);
    EXPECT_EQ(bird.type, 0x173);
    EXPECT_EQ(bird.active, 1);
    EXPECT_EQ(bird.position, glm::vec3(-162, 564, -800));
    EXPECT_FLOAT_EQ(bird.angles.z, -108);
    EXPECT_FLOAT_EQ(bird.bodyScale, 2.9f);
    EXPECT_FLOAT_EQ(bird.scale, .56f);
    EXPECT_FLOAT_EQ(bird.turnRate, -1.5f);
    EXPECT_EQ(bird.timer, 200);
    EXPECT_EQ(bird.hiddenMesh, 5);
    Client::Render::Models::CrtRandom expected;
    for (int i = 0; i < 10; ++i)
        static_cast<void>(expected.Next());
    EXPECT_EQ(m_cloth.random.State(), expected.State());
}
TEST_F(FaunaStepTest, MissingJointBindingStopsAfterTheIcarusBirthPrefix)
{
    m_map = 10;
    auto context = Context();
    auto& bird = m_fauna.Birds()[3];
    bird.active = 1;
    const auto result = RunFaunaBirdHandler(context, Client::World::BirdHandler::SpawnIcarus, bird, 3);
    EXPECT_EQ(result.error, std::make_error_code(std::errc::function_not_supported));
    EXPECT_EQ(bird.type, 0x10f);
    EXPECT_EQ(bird.timer, 9600);
    EXPECT_EQ(bird.position, glm::vec3(523, 678, 0));
    EXPECT_FLOAT_EQ(bird.angles.z, 0);
    Client::Render::Models::CrtRandom expected;
    for (int i = 0; i < 3; ++i)
        static_cast<void>(expected.Next());
    EXPECT_EQ(m_cloth.random.State(), expected.State());
}
TEST_F(FaunaStepTest, IntegerSteeringPreservesTheNativeEqualityAndOppositeTies)
{
    EXPECT_EQ(Client::Scenes::TurnFaunaAngle(90, 90, 13), 180);
    EXPECT_EQ(Client::Scenes::TurnFaunaAngle(0, 180, 13), 347);
    EXPECT_EQ(Client::Scenes::TurnFaunaAngle(180, 0, 13), 193);
    EXPECT_EQ(Client::Scenes::TurnFaunaAngle(359, 0, 13), 12);
}
TEST_F(FaunaStepTest, TheOrdinaryFamiliesAndAtlansRunBothRealSteps)
{
    for (const std::int32_t map : {0, 1, 3, 4, 7})
    {
        m_fauna.Clear();
        m_map = map;
        auto context = Context();
        const auto result = StepGameFauna(context);
        ASSERT_TRUE(result.HasValue()) << map << ": " << result.Error();
        EXPECT_NE(m_fauna.Birds()[0].type, 0);
        if (map == 1 || map == 7)
        {
            EXPECT_NE(m_fauna.Fish()[0].active, 0);
        }
    }
}
} // namespace

namespace
{
TEST_F(FaunaStepTest, KalimaSetActionNeedsTheExplicitSignedPrefixModelLoan)
{
    m_map = 24;
    m_models.missingPrefix = true;
    auto context = Context();
    context.effects.effect =
        [](const Client::World::FishEffectRequest&) -> Client::Core::Expected<std::uint32_t, std::error_code>
    { return std::uint32_t{0}; };
    const auto missing = StepGameFish(context);
    EXPECT_FALSE(missing.HasValue());
    EXPECT_EQ(m_fauna.Fish()[0].type, -1);
    EXPECT_TRUE(m_fauna.Fish()[0].active);
    const Client::Render::Models::Skeleton prefix;
    context.prefixModel = &prefix;
    const auto borrowed = StepGameFish(context);
    ASSERT_TRUE(borrowed.HasValue()) << borrowed.Error();
    EXPECT_EQ(m_fauna.Fish()[0].type, -1);
}
} // namespace

namespace
{
TEST_F(FaunaStepTest, RealBirdAndFishHeightServicesConsumeTheDurableFixedSurface)
{
    m_terrain.attributes->cells.fill(0x40);
    auto context = Context();
    context.fixedHeight = 1201;
    auto& bird = m_fauna.Birds()[0];
    bird.position = {1000, 1000, 0};
    ASSERT_FALSE(RunFaunaBirdHandler(context, Client::World::BirdHandler::MoveAf, bird, 0).error);
    EXPECT_FLOAT_EQ(bird.position.z, 1551);
    m_map = 1;
    auto& liveFish = m_fauna.Fish()[0];
    liveFish.active = 1;
    liveFish.type = 0xb0;
    liveFish.position = {1000, 1000, 0};
    liveFish.scale = 1;
    liveFish.bodyScale = 1;
    liveFish.turnRate = 13;
    liveFish.timer = 1; // b0 sólo toma FullMovement mientras timer>0.
    const auto fish = StepGameFish(context);
    ASSERT_TRUE(fish.HasValue()) << fish.Error();
    EXPECT_FLOAT_EQ(m_fauna.Fish()[0].position.z, 1201);
}
} // namespace
