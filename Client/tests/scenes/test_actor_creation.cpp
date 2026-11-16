#include "Scenes/ActorCreation.h"

#include "Scenes/ActorState.h"
#include "Scenes/HelperTable.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

namespace
{
using Client::Scenes::ActorState;
using Client::Scenes::RemoveActorAtSlot;
using Client::Scenes::WorldState;

TEST(ActorCreationTest, RemovesTheRequestedSlotWithoutResettingItsOtherState)
{
    WorldState world;
    ActorState& actor = world.Actors()[1];
    actor.alive = true;
    actor.key = 77;
    actor.position = {10.0f, 20.0f, 30.0f};
    actor.dword428 = 0;
    actor.darkSpirit.emplace();
    actor.layer.emplace();
    actor.byte360 = 0;
    ActorState& duplicateKey = world.Actors()[2];
    duplicateKey.alive = true;
    duplicateKey.key = 77;
    world.Helpers()[0] = {.alive = true, .type = 0xbf, .rider = 1};
    world.Helpers()[1] = {.alive = true, .type = 0xbf, .rider = 2};
    world.Runtime().namedBones = {{1, "RightHand", 3}, {1, "LeftHand", 4}, {2, "LeftHand", 4}};

    RemoveActorAtSlot(world, actor, 1);

    EXPECT_FALSE(actor.alive);
    EXPECT_EQ(actor.key, 77u);
    EXPECT_EQ(actor.position, (glm::vec3{10.0f, 20.0f, 30.0f}));
    EXPECT_EQ(actor.dword428, 0);
    EXPECT_FALSE(actor.darkSpirit.has_value());
    EXPECT_FALSE(actor.layer.has_value());
    EXPECT_EQ(actor.byte360, 0);
    EXPECT_FALSE(world.Helpers()[0].alive);
    EXPECT_TRUE(world.Helpers()[1].alive);
    EXPECT_TRUE(duplicateKey.alive);
    ASSERT_EQ(world.Runtime().namedBones.size(), 1u);
    EXPECT_EQ(world.Runtime().namedBones[0].actor, 2u);
}

TEST(ActorCreationTest, LeavesTheLayerByteAloneWhenTheSlotHasNoLayer)
{
    WorldState world;
    ActorState& actor = world.Actors()[1];
    actor.alive = true;
    actor.byte360 = 6;

    RemoveActorAtSlot(world, actor, 1);

    EXPECT_FALSE(actor.alive);
    EXPECT_FALSE(actor.layer.has_value());
    EXPECT_EQ(actor.byte360, 6);
}

TEST(ActorCreationTest, CleansTheDirectReferenceWithoutIndexingWorldResourcesAtTheSentinel)
{
    WorldState world;
    ActorState& sentinel = world.Actors()[Client::Scenes::CreatedActorSearch];
    sentinel.alive = true;
    sentinel.darkSpirit.emplace();
    sentinel.layer.emplace();
    world.Helpers()[0] = {.alive = true, .type = 0xbf, .rider = Client::Scenes::CreatedActorSearch};
    world.Runtime().namedBones = {{Client::Scenes::CreatedActorSearch, "RightHand", 3}};

    RemoveActorAtSlot(world, sentinel, Client::Scenes::CreatedActorSearch);

    EXPECT_FALSE(sentinel.alive);
    EXPECT_FALSE(sentinel.darkSpirit.has_value());
    EXPECT_FALSE(sentinel.layer.has_value());
    EXPECT_TRUE(world.Helpers()[0].alive);
    ASSERT_EQ(world.Runtime().namedBones.size(), 1u);
}
} // namespace
