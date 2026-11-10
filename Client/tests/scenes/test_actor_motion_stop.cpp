#include "Scenes/ActorActions.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldRuntime.h"
#include "World/TerrainWalls.h"
#include "support/PlayerActors.h"

#include <gtest/gtest.h>

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
    Client::Render::Models::CrtRandom random(1);
    const Client::Scenes::ActorStopContext context{runtime,    Client::Scenes::SceneId::Game, skeleton, nullptr, random,
                                                   walls.get()};
    const auto flying = Client::Scenes::SetActorStop(actor, context);
    ASSERT_TRUE(flying.HasValue()) << (flying.HasValue() ? "" : flying.Error());
    EXPECT_EQ(actor.animation.action, 9);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 1).State());
    (*walls)[Client::Data::Terrain::CellIndex(10, 20)] = Client::World::SafeZoneWallBit;
    const auto standing = Client::Scenes::SetActorStop(actor, context);
    ASSERT_TRUE(standing.HasValue()) << (standing.HasValue() ? "" : standing.Error());
    EXPECT_EQ(actor.animation.action, 1);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 2).State());
}
