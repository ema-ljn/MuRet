#include "WorldObjects/WorldModelPose.h"

#include "Math/HalfLifeMath.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Assets/FaunaModels.h"
#include "Kernel/WorldState.h"

#include <gtest/gtest.h>
#include <glm/ext/vector_float3.hpp>

#include <memory>
#include <string>

namespace
{
TEST(WorldModelPoseTest, AnInvalidPublishedActionIsClampedInTheSharedModel)
{
    auto world = std::make_unique<Client::Kernel::WorldState>();
    Client::Assets::FaunaModels faunaModels;
    Client::Kernel::ActorState actor;
    actor.type = 0x1b3;
    actor.animation.action = 7;
    actor.scale = 2.0f;
    actor.position = {100, 200, 300};
    world->PlaceModel(actor);
    Client::Models::Skeleton skeleton;
    skeleton.actions.resize(2, {.keyCount = 10});

    const auto posed = Client::Scenes::PoseWorldModel(*world, &faunaModels, actor, skeleton, 3.25f, actor.angles);

    ASSERT_TRUE(posed.HasValue()) << posed.Error();
    EXPECT_EQ(world->ModelPlacement(actor.type).action, 0);
    EXPECT_EQ(actor.animation.action, 7);
    EXPECT_EQ(faunaModels.PoseKey(actor.type), 3);
    EXPECT_EQ(faunaModels.PoseFrame(actor.type), 3.25f);
}

TEST(WorldModelPoseTest, AModelWithoutActionsLeavesTheGlobalPoseAndModelUntouched)
{
    auto world = std::make_unique<Client::Kernel::WorldState>();
    Client::Assets::FaunaModels faunaModels;
    Client::Kernel::ActorState actor;
    actor.type = 0x1b3;
    actor.animation.action = 7;
    actor.scale = 2.0f;
    world->PlaceModel(actor);
    world->PoseCarry().bones.assign(1, Client::Math::BoneTransform(1.0f));
    faunaModels.RecordAnimation(actor.type, 6.5f, 9);
    Client::Models::Skeleton skeleton;

    const auto posed = Client::Scenes::PoseWorldModel(*world, &faunaModels, actor, skeleton, 3.25f, actor.angles);

    ASSERT_TRUE(posed.HasValue()) << posed.Error();
    EXPECT_EQ(world->ModelPlacement(actor.type).action, 7);
    EXPECT_EQ(faunaModels.PoseKey(actor.type), 9);
    EXPECT_EQ(faunaModels.PoseFrame(actor.type), 6.5f);
    EXPECT_EQ(world->PoseCarry().bones[0], Client::Math::BoneTransform(1.0f));
    EXPECT_FALSE(world->PoseCarry().models.contains(actor.type));
}

TEST(WorldModelPoseTest, WithoutTheFaunaModelsThePoseStopsBeforeWritingTheSharedModel)
{
    auto world = std::make_unique<Client::Kernel::WorldState>();
    Client::Kernel::ActorState actor;
    actor.type = 0x1b3;
    actor.animation.action = 7;
    world->PlaceModel(actor);
    Client::Models::Skeleton skeleton;
    skeleton.actions.resize(2, {.keyCount = 10});

    const auto posed = Client::Scenes::PoseWorldModel(*world, nullptr, actor, skeleton, 3.25f, actor.angles);

    ASSERT_FALSE(posed.HasValue());
    EXPECT_NE(posed.Error().find("0x5769ad8"), std::string::npos) << posed.Error();
    EXPECT_EQ(world->ModelPlacement(actor.type).action, 7);
    EXPECT_FALSE(world->PoseCarry().models.contains(actor.type));
}
} // namespace
