#include "Scenes/ActorActions.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Scenes/ActorMotion.h"
#include "Scenes/ActorState.h"
#include "Scenes/ActorWalk.h"
#include "Scenes/PlayerActionSpeeds.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldRuntime.h"
#include "Pathfinding/TerrainWalls.h"
#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>

TEST(ActorMotionStopTest, SwimmingMapReadsTheVisiblePositionAttributeWithoutLogicalCellRandom)
{
    auto walls = std::make_unique<Client::Data::Terrain::TerrainLayer<std::uint16_t>>();
    auto actor = Client::Tests::UnarmedPlayer();
    actor.position = {1050.0f, 2050.0f, 0.0f};
    actor.cellX = 11;
    actor.cellY = 21;
    Client::Scenes::WorldRuntime runtime{};
    runtime.map = 7;
    auto skeleton = Client::Tests::PlayerActionsSkeleton();
    Client::Crt::CrtRandom random(1);
    const Client::Scenes::ActorStopContext context{runtime,    Client::Scenes::SceneId::Game, skeleton, nullptr, random,
                                                   walls.get()};
    const auto flying = Client::Scenes::SetActorStop(actor, context);
    ASSERT_TRUE(flying.HasValue()) << (flying.HasValue() ? "" : flying.Error());
    EXPECT_EQ(actor.animation.action, 9);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 1).State());
    (*walls)[Client::Data::Terrain::CellIndex(10, 20)] = Client::Pathfinding::SafeZoneWallBit;
    const auto standing = Client::Scenes::SetActorStop(actor, context);
    ASSERT_TRUE(standing.HasValue()) << (standing.HasValue() ? "" : standing.Error());
    EXPECT_EQ(actor.animation.action, 1);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 2).State());
}

TEST(ActorMotionStopTest, PlayerSubtype209CarriesItsModelSoundsThroughArrivalStop)
{
    auto actor = Client::Tests::UnarmedPlayer();
    actor.subtype = 209;
    actor.byte88 = 1;
    actor.movement = 1;
    actor.direction = 0;
    actor.position = {1150.0f, 2050.0f, 0.0f};
    actor.path.count = 2;
    actor.path.progress = 3;
    actor.path.cells[0] = {10, 20};
    actor.path.cells[1] = {11, 20};
    Client::Scenes::WorldRuntime runtime{};
    runtime.map = 0;
    auto player = Client::Tests::PlayerActionsSkeleton();
    Client::Crt::CrtRandom random(1);
    Client::Scenes::PlayerActionSpeeds speeds;
    const std::array<std::int16_t, 10> sounds{};
    const Client::Scenes::ActorWalkContext walk{
        {runtime, Client::Scenes::SceneId::Game, player, nullptr, random}, speeds, false, nullptr, sounds};
    auto walls = std::make_unique<Client::Data::Terrain::TerrainLayer<std::uint16_t>>();

    const auto stepped =
        Client::Scenes::StepOtherActorMotion(actor, walk, {runtime, random, [](float, float) { return 7.0; }}, *walls);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(actor.movement, 0);
    EXPECT_EQ(actor.animation.action, 1);
    EXPECT_FLOAT_EQ(actor.angles.z, -45.0f);
    EXPECT_NEAR(actor.position.x, 1150.0f - 8.485281f, 0.001f);
    EXPECT_NEAR(actor.position.y, 2050.0f - 8.485281f, 0.001f);
    EXPECT_FLOAT_EQ(actor.position.z, 7.0f);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 11).State());
}
