#include "Scenes/ActorMotion.h"

#include "Core/Expected.h"
#include "GameLogic/Path.h"
#include "GameLogic/PathFinder.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/ActorWalk.h"
#include "Scenes/PlayerActionSpeeds.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldRuntime.h"
#include "World/TerrainWalls.h"
#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

namespace
{
class ActorMotionTest : public ::testing::Test
{
protected:
    void Step(Client::Scenes::ActorState& actor)
    {
        const Client::Scenes::ActorWalkContext walk{
            {m_runtime, Client::Scenes::SceneId::Game, m_skeleton, nullptr, m_random}, m_speeds, false};
        auto result = Client::Scenes::StepOtherActorMotion(
            actor, walk, {m_runtime, m_random, [](float, float) { return 7.0; }}, m_walls->cells);
        ASSERT_TRUE(result.HasValue()) << (result.HasValue() ? "" : result.Error());
    }
    Client::Scenes::WorldRuntime m_runtime = []
    {
        Client::Scenes::WorldRuntime value{};
        value.map = 0;
        return value;
    }();
    Client::Render::Models::Skeleton m_skeleton = Client::Tests::PlayerActionsSkeleton();
    Client::Render::Models::CrtRandom m_random{1};
    Client::Scenes::PlayerActionSpeeds m_speeds;
    std::unique_ptr<Client::World::TerrainWalls> m_walls = std::make_unique<Client::World::TerrainWalls>();
};

TEST_F(ActorMotionTest, BeginningStoresCellAheadAndLeavesTheAuthoritativeTarget)
{
    auto actor = Client::Tests::UnarmedPlayer();
    actor.cellX = 10;
    actor.cellY = 20;
    actor.targetX = 12;
    actor.targetY = 20;
    actor.position = {1050.0f, 2050.0f, 0.0f};
    actor.path.count = 3;
    actor.path.cells[0] = {10, 20};
    actor.path.cells[1] = {11, 20};
    actor.path.cells[2] = {12, 20};
    EXPECT_FALSE(Client::Scenes::MoveActorPath(actor, 1, m_random));
    EXPECT_EQ(actor.cellX, 11);
    EXPECT_EQ(actor.cellY, 20);
    EXPECT_EQ(actor.targetX, 12);
    EXPECT_FLOAT_EQ(actor.position.x, 1050.0f);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(1, 8).State());
    actor.cellXAccesses = 1;
    actor.cellYAccesses = 1;
    EXPECT_FALSE(Client::Scenes::MoveActorPath(actor, 1, m_random));
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(1, 8).State());
}

TEST_F(ActorMotionTest, PositionUsesSharedSpeedAndTheFloatingHeight)
{
    auto actor = Client::Tests::UnarmedPlayer();
    actor.byte88 = 1;
    actor.position = {1050.0f, 2050.0f, 900.0f};
    actor.angles.z = 90.0f;
    actor.pet.model = 0x1c15;
    m_runtime.map = 8;
    auto moved = Client::Scenes::UpdateActorPosition(actor, {m_runtime, m_random, [](float, float) { return 7.0; }});
    ASSERT_TRUE(moved.HasValue());
    EXPECT_FLOAT_EQ(actor.position.x, 1065.0f);
    EXPECT_FLOAT_EQ(actor.position.z, 97.0f);
    EXPECT_FLOAT_EQ(actor.movementPhase, 0.15f);
    EXPECT_EQ(actor.run, 40);
    EXPECT_EQ(m_random.State(), 1U);
}

TEST_F(ActorMotionTest, OtherArrivalStopsSetsMinus45ThenStillMoves)
{
    auto actor = Client::Tests::UnarmedPlayer();
    actor.byte88 = 1;
    actor.movement = 1;
    actor.direction = 0;
    actor.position = {1150.0f, 2050.0f, 0.0f};
    actor.path.count = 2;
    actor.path.progress = 3;
    actor.path.cells[0] = {10, 20};
    actor.path.cells[1] = {11, 20};
    Step(actor);
    EXPECT_EQ(actor.movement, 0);
    EXPECT_EQ(actor.animation.action, 1);
    EXPECT_FLOAT_EQ(actor.angles.z, -45.0f);
    EXPECT_NEAR(actor.position.x, 1150.0f - 8.485281f, 0.001f);
    EXPECT_NEAR(actor.position.y, 2050.0f - 8.485281f, 0.001f);
    EXPECT_FLOAT_EQ(actor.position.z, 7.0f);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(1, 10).State());
}

TEST_F(ActorMotionTest, RetryIsRepeatedOnFailureAndSuccessfulSearchWaitsOneStep)
{
    auto actor = Client::Tests::UnarmedPlayer();
    actor.cellX = 10;
    actor.cellY = 20;
    actor.targetX = 12;
    actor.targetY = 20;
    actor.position = {1050.0f, 2050.0f, 0.0f};
    m_walls->cells.fill(Client::World::BlockedWallBit);
    Step(actor);
    EXPECT_EQ(actor.movement, 0);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(1, 12).State());
    Step(actor);
    EXPECT_EQ(actor.movement, 0);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(1, 24).State());
    m_walls->cells.fill(0);
    Step(actor);
    EXPECT_EQ(actor.movement, 1);
    EXPECT_GT(actor.path.count, 0);
    EXPECT_FLOAT_EQ(actor.position.x, 1050.0f);
    EXPECT_FLOAT_EQ(actor.movementPhase, 0.0f);
}

TEST_F(ActorMotionTest, CooldownOrPathlessActorSkipsTheLogicalReads)
{
    auto actor = Client::Tests::UnarmedPlayer();
    actor.cellX = 10;
    actor.targetX = 20;
    actor.movementCooldown = 1;
    Step(actor);
    EXPECT_EQ(m_random.State(), 1U);
    EXPECT_EQ(actor.movementCooldown, 1);
    actor.movementCooldown = 0;
    actor.type = 0x159;
    Step(actor);
    EXPECT_EQ(m_random.State(), 1U);
}
} // namespace

TEST(ActorMotionPathFinderTest, RawTerrainGridConstructorClearsTheLiveGoal)
{
    auto cells = std::make_unique<Client::Data::Terrain::TerrainLayer<std::uint16_t>>();
    const auto goal = Client::Data::Terrain::CellIndex(12, 20);
    (*cells)[goal] = Client::World::BlockedWallBit;
    const auto path = Client::GameLogic::PathFinder(*cells).Find({10, 20}, {12, 20}, true);
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ((*cells)[goal], 0);
}

TEST_F(ActorMotionTest, MissingHeightRetainsTheSpeedAndPositionPrefix)
{
    auto actor = Client::Tests::UnarmedPlayer();
    actor.byte88 = 1;
    actor.pet.model = 0x1c16;
    actor.position = {100.0f, 200.0f, 3.0f};
    actor.angles.z = 90.0f;
    const auto moved = Client::Scenes::UpdateActorPosition(actor, {m_runtime, m_random, {}});
    EXPECT_FALSE(moved.HasValue());
    EXPECT_FLOAT_EQ(actor.position.x, 117.0f);
    EXPECT_FLOAT_EQ(actor.position.y, 200.0f);
    EXPECT_FLOAT_EQ(actor.position.z, 3.0f);
    EXPECT_FLOAT_EQ(actor.movementPhase, 0.0f);
    EXPECT_EQ(actor.run, 40);
}

TEST_F(ActorMotionTest, SixStraightCellsUseTwelveUnitsUntilTheFinalSnap)
{
    auto actor = Client::Tests::UnarmedPlayer();
    actor.byte88 = 1;
    actor.position = {1050.0f, 2050.0f, 0.0f};
    actor.angles.z = 90.0f;
    actor.path.count = 7;
    for (std::uint8_t cell = 0; cell < actor.path.count; ++cell)
    {
        actor.path.cells[cell] = {static_cast<std::uint8_t>(10 + cell), 20};
    }
    const Client::Scenes::ActorWalkContext walk{
        {m_runtime, Client::Scenes::SceneId::Game, m_skeleton, nullptr, m_random}, m_speeds, true};
    bool arrived = false;
    int steps = 0;
    while (!arrived && steps < 100)
    {
        ++steps;
        ASSERT_TRUE(Client::Scenes::SetActorWalk(actor, walk).HasValue());
        arrived = Client::Scenes::MoveActorPath(actor, 1, m_random);
        if (!arrived)
        {
            ASSERT_TRUE(
                Client::Scenes::UpdateActorPosition(actor, {m_runtime, m_random, [](float, float) { return 7.0; }})
                    .HasValue());
        }
    }
    ASSERT_TRUE(arrived);
    // Desde el centro exacto: 49 avances de12 dejan588/600; la llamada50 hace snap desde12.
    // El research histórico dice49, pero no proporciona su posición/fase inicial para reproducirla.
    EXPECT_EQ(steps, 50);
    EXPECT_FLOAT_EQ(actor.position.x, 1650.0f);
    EXPECT_FLOAT_EQ(actor.position.y, 2050.0f);
    EXPECT_EQ(actor.cellX, 16);
    EXPECT_EQ(actor.cellY, 20);
}
