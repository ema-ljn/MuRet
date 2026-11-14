#include "Scenes/ActorConstruction.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "World/TerrainCulling.h"
#include "World/WorldTerrain.h"

#include <gtest/gtest.h>

#include <memory>

TEST(ActorMotionConstructionTest, ResetsWrittenFieldsAndRetainsUnwrittenMotionHistory)
{
    Client::Scenes::ActorState actor;
    actor.path.current = 4;
    actor.path.count = 8;
    actor.path.progress = 3;
    actor.targetX = 80;
    actor.targetY = 90;
    actor.movementPhase = 9.0f;
    actor.baseMovementSpeed = 999;
    actor.direction = 7;
    actor.movementCooldown = 21;
    actor.movementType = 6;
    actor.damage = 123;
    actor.byte369 = 7;
    actor.byte370 = 8;
    actor.byte372 = 6;
    actor.byte373 = 2;
    actor.byte374 = 7;
    actor.word1f2 = 4;
    actor.byte399 = 1;
    actor.byte80 = 3;
    actor.skillIndex = 5;
    actor.firstFootstepSound = 7;
    actor.secondFootstepSound = 9;
    Client::World::WorldTerrain terrain;
    terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    Client::Render::Models::ClothWorld cloth;
    const auto built = Client::Scenes::ConstructActor(actor, 0x20a, {10, 20, 0.0f},
                                                      {terrain, 0, Client::World::CullingScene::Other, 1200.0f}, cloth);
    ASSERT_TRUE(built.HasValue()) << (built.HasValue() ? "" : built.Error());
    EXPECT_EQ(actor.path.current, 0);
    EXPECT_EQ(actor.path.count, 0);
    EXPECT_EQ(actor.path.progress, 3);
    EXPECT_EQ(actor.targetX, 10);
    EXPECT_EQ(actor.targetY, 20);
    EXPECT_FLOAT_EQ(actor.movementPhase, 0.0f);
    EXPECT_EQ(actor.baseMovementSpeed, 10);
    EXPECT_EQ(actor.direction, 7);
    EXPECT_EQ(actor.movementCooldown, 21);
    EXPECT_EQ(actor.movementType, 6);
    EXPECT_EQ(actor.damage, 123);
    EXPECT_EQ(actor.byte369, 0);
    EXPECT_EQ(actor.byte370, 0);
    // 0x511ccf y 0x511cc8 (CL9 hito 12): la cuenta del B8 en 0 y la habilidad guardada en 0xff.
    EXPECT_EQ(actor.byte372, 0);
    EXPECT_EQ(actor.byte373, 0xff);
    EXPECT_EQ(actor.byte374, 0);
    EXPECT_EQ(actor.word1f2, 0xffff);
    EXPECT_EQ(actor.byte399, 0);
    EXPECT_EQ(actor.byte80, 0);
    EXPECT_EQ(actor.skillIndex, 5);
    EXPECT_EQ(actor.firstFootstepSound, 7);
    EXPECT_EQ(actor.secondFootstepSound, 9);
}
