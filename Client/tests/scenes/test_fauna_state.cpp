#include "Render/Models/HalfLifeMath.h"
#include "Scenes/FaunaState.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

namespace
{
// 60712a..607150 sólo escribe byte vivo; el dibujo conserva la historia de los slots muertos.
TEST(FaunaStateTest, ClearingPreservesPhysicalRecordsAndTheirHistory)
{
    Client::Scenes::FaunaState fauna;
    auto birds = fauna.Birds();
    auto fish = fauna.Fish();
    birds[39].active = 1;
    birds[39].angles.z = 0.001f;
    fish[9].active = 1;
    fish[9].previousFrame = 0.75f;
    const auto* bird = &birds[39];
    const auto* lastFish = &fish[9];

    fauna.Clear();

    EXPECT_EQ(fauna.Birds()[39].active, 0);
    EXPECT_FLOAT_EQ(fauna.Birds()[39].angles.z, 0.001f);
    EXPECT_EQ(fauna.Fish()[9].active, 0);
    EXPECT_FLOAT_EQ(fauna.Fish()[9].previousFrame, 0.75f);
    EXPECT_EQ(&fauna.Birds()[39], bird);
    EXPECT_EQ(&fauna.Fish()[9], lastFish);
    EXPECT_EQ(reinterpret_cast<const std::byte*>(&birds[1]) - reinterpret_cast<const std::byte*>(&birds[0]), 0x1d4);
}

TEST(FaunaStateTest, ViewsBorrowCanonicalStorageAndKeepEffectIdentity)
{
    Client::Scenes::FaunaState fauna;
    auto birds = fauna.BirdRecords();
    auto fish = fauna.FishViews();
    EXPECT_EQ(birds[1].data() - birds[0].data(), 0x1d4);
    EXPECT_EQ(fish[1].context, &fauna.Fish()[1]);
    fish[1].position.x = 12;
    EXPECT_FLOAT_EQ(fauna.Fish()[1].position.x, 12);
    fish[1].speed = 2;
    EXPECT_FLOAT_EQ(fauna.Fish()[1].scale, 2);
}
} // namespace

#include "Scenes/WorldState.h"
namespace
{
TEST(FaunaStateTest, PoseScratchBelongsToTheDurableWorldAndSurvivesFaunaClear)
{
    Client::Scenes::WorldState world;
    auto& pose = world.PoseCarry();
    pose.bones.resize(3, Client::Render::Models::BoneTransform(0));
    pose.bones[2][3] = {7, 8, 9};
    world.Fauna().Clear();
    EXPECT_EQ(&world.PoseCarry(), &pose);
    EXPECT_EQ(world.PoseCarry().bones[2][3], glm::vec3(7, 8, 9));
}
} // namespace
