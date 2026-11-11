#include "Scenes/GameDrawTail.h"
#include "Scenes/FaunaObject.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "World/Sprites.h"
#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <span>

namespace
{
Client::Scenes::JointState Joint(int type, int subtype = 0)
{
    Client::Scenes::JointState joint;
    joint.alive = true;
    joint.count = 2;
    joint.maximumCount = 5;
    joint.drawEnabled = 3;
    joint.type = type;
    joint.subtype = subtype;
    joint.texture = static_cast<std::uint16_t>(type);
    joint.light = {0.5f, 0.6f, 0.7f};
    joint.life = 10;
    return joint;
}
} // namespace

TEST(GameJointDrawingTest, RepeatedUvsUseOneSixteenthBeforeDoubling)
{
    auto joint = Joint(0x617);
    joint.repeatedTexture = true;
    Client::Render::ModelBatch batch;
    ASSERT_TRUE(Client::Scenes::DrawJoints(batch, std::span(&joint, 1), 0).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 24u);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].uv.x, 0.25f);
    EXPECT_FLOAT_EQ(batch.Vertices()[2].uv.x, 0.125f);
}

TEST(GameJointDrawingTest, SparkSkipsItsFirstSegmentAndUsesLifeTimesOneTenth)
{
    auto joint = Joint(0x5f2);
    Client::Render::ModelBatch batch;
    ASSERT_TRUE(Client::Scenes::DrawJoints(batch, std::span(&joint, 1), 0).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 12u);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 0.5f);
}

TEST(GameJointDrawingTest, InverseColorJointOverridesItsConfiguredBlend)
{
    auto joint = Joint(0x62b, 20);
    joint.blend = 2;
    Client::Render::ModelBatch batch;
    ASSERT_TRUE(Client::Scenes::DrawJoints(batch, std::span(&joint, 1), 0).HasValue());
    EXPECT_EQ(batch.State().blend, Client::Render::ModelBlend::InverseColorAdditive);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.green, 0.6f);
}

TEST(GameJointDrawingTest, LaserEmitsOnlyItsSingleFaceWithSegmentGray)
{
    auto joint = Joint(0x61c);
    joint.drawEnabled = 0x80;
    joint.points[0][0] = {7, 8, 9};
    Client::Render::ModelBatch batch;
    ASSERT_TRUE(Client::Scenes::DrawJoints(batch, std::span(&joint, 1), 0).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 12u);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].position.x, 7);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 1);
    EXPECT_FLOAT_EQ(batch.Vertices()[6].color.red, 0.8f);
}

TEST(GameJointDrawingTest, AnimatedFlareDoublesItsUvsAndSpiritUsesHalfAnIntegerDivisor)
{
    for (const auto type : {0x614, 0x615, 0x62c})
    {
        auto joint = Joint(type, type == 0x615 ? 1 : 0);
        Client::Render::ModelBatch batch;
        ASSERT_TRUE(Client::Scenes::DrawJoints(batch, std::span(&joint, 1), 0).HasValue());
        ASSERT_EQ(batch.Vertices().size(), 24u);
        EXPECT_FLOAT_EQ(batch.Vertices()[0].uv.x, 1);
        EXPECT_FLOAT_EQ(batch.Vertices()[2].uv.x, 0.5f);
        if (type == 0x62c)
        {
            EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 0.2f);
            EXPECT_FLOAT_EQ(batch.Vertices()[12].color.red, 0);
        }
    }
}

TEST(GameJointDrawingTest, FlareZeroSelectsItsGrayAtEachSegment)
{
    auto joint = Joint(0x615);
    joint.light.z = 0.25f;
    Client::Render::ModelBatch batch;
    ASSERT_TRUE(Client::Scenes::DrawJoints(batch, std::span(&joint, 1), 0).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 24u);
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 0.25f);
    EXPECT_FLOAT_EQ(batch.Vertices()[12].color.red, 0);
}

TEST(GameJointDrawingTest, AnimatedUvsUseSignedClockRemainderWithoutRandoms)
{
    Client::Render::Models::CrtRandom random(9);
    Client::Render::Models::CrtRandom expected(9);
    Client::World::FrameSprites sprites;
    Client::Scenes::JointDrawResources resources{random, sprites, true, -1250.75f};
    auto joint = Joint(0x614, 1);
    Client::Render::ModelBatch batch;
    ASSERT_TRUE(Client::Scenes::DrawJoints(batch, std::span(&joint, 1), 0, resources).HasValue());
    EXPECT_FLOAT_EQ(batch.Vertices()[0].uv.x, 1.25f);
    EXPECT_EQ(random.Next(), expected.Next());
    EXPECT_FALSE(sprites.Any());
}

TEST(GameJointDrawingTest, BloodCastleOwnerHeightSetsPerSegmentColorAndMiddleSprite)
{
    Client::Render::Models::CrtRandom random(9);
    Client::Render::Models::CrtRandom expected(9);
    Client::World::FrameSprites sprites;
    const Client::Scenes::JointDrawResources resources{random, sprites};
    auto joint = Joint(0x10f, 0);
    joint.points[0][0].z = 30;
    joint.points[1][0].z = 50;
    joint.ownerContext = nullptr;
    Client::Render::ModelBatch batch;
    ASSERT_TRUE(Client::Scenes::DrawJoints(batch, std::span(&joint, 1), 0, resources).HasValue());
    EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 1);
    ASSERT_TRUE(sprites.Any());
    EXPECT_EQ(sprites.Sprites()[0].texture, 0x62b);
    EXPECT_FLOAT_EQ(sprites.Sprites()[0].scale, 0.7f);
    EXPECT_FLOAT_EQ(sprites.Sprites()[0].position.z, 12.5f);
    EXPECT_EQ(random.Next(), expected.Next());
}

TEST(GameJointDrawingTest, ModeFiltersRunBeforeSpecialResourcesAndPreserveLiveJoints)
{
    auto joint = Joint(0x10f, 0);
    joint.category = 2;
    Client::Render::ModelBatch batch;
    EXPECT_TRUE(Client::Scenes::DrawJoints(batch, std::span(&joint, 1), 1).HasValue());
    EXPECT_TRUE(batch.Commands().empty());
    joint.category = 1;
    EXPECT_TRUE(Client::Scenes::DrawJoints(batch, std::span(&joint, 1), 2).HasValue());
    EXPECT_TRUE(batch.Commands().empty());
    EXPECT_TRUE(joint.alive);
}

TEST(GameJointDrawingTest, IntermediateSpecialSpritesRepeatWithoutChangingTheRandomStream)
{
    auto joint = Joint(0x10f, 9);
    Client::Render::Models::CrtRandom random(7);
    Client::Render::Models::CrtRandom expected(7);
    Client::World::FrameSprites sprites;
    Client::Render::ModelBatch original;
    Client::Render::ModelBatch replay;
    ASSERT_TRUE(Client::Scenes::DrawJoints(original, std::span(&joint, 1), 0, {random, sprites, true}).HasValue());
    const auto light = sprites.Sprites()[0].light;
    sprites.Clear();
    ASSERT_TRUE(Client::Scenes::DrawJoints(replay, std::span(&joint, 1), 0, {random, sprites, false}).HasValue());
    EXPECT_EQ(replay.Vertices().size(), original.Vertices().size());
    EXPECT_EQ(sprites.Sprites()[0].light, light);
    EXPECT_EQ(random.Next(), expected.Next());
}

TEST(GameJointDrawingTest, OwnerHeightUsesFiftyForTheFirstFamilyAndOneEightyForNine)
{
    Client::Scenes::FaunaObject owner;
    owner.position.z = 10;
    for (const int subtype : {0, 4, 9})
    {
        auto joint = Joint(0x10f, subtype);
        joint.ownerContext = &owner;
        joint.points[0][0].z = subtype == 9 ? 215.0f : 85.0f;
        Client::Render::Models::CrtRandom random(1);
        Client::World::FrameSprites sprites;
        Client::Render::ModelBatch batch;
        ASSERT_TRUE(Client::Scenes::DrawJoints(batch, std::span(&joint, 1), 0, {random, sprites}).HasValue());
        EXPECT_FLOAT_EQ(batch.Vertices()[0].color.red, 0.25f);
        EXPECT_FLOAT_EQ(batch.Vertices()[0].color.green, 0.35f);
    }
}

TEST(GameJointDrawingTest, EnergyArrowMutatesItsLightAndEmitsOneSpritePerAdmittedSegment)
{
    auto joint = Joint(0x619, 9);
    Client::Scenes::FaunaObject owner;
    joint.ownerContext = &owner;
    joint.count = 3;
    joint.light = glm::vec3(0.5f);
    Client::Render::Models::CrtRandom random(5);
    Client::Render::Models::CrtRandom expected(5);
    Client::World::FrameSprites sprites;
    Client::Render::ModelBatch batch;
    ASSERT_TRUE(Client::Scenes::DrawJoints(batch, std::span(&joint, 1), 0, {random, sprites}).HasValue());
    EXPECT_EQ(batch.Vertices().size(), 24u);
    EXPECT_TRUE(sprites.Sprites()[0].alive);
    EXPECT_TRUE(sprites.Sprites()[1].alive);
    EXPECT_FALSE(sprites.Sprites()[2].alive);
    EXPECT_EQ(sprites.Sprites()[0].texture, 0x62b);
    EXPECT_FLOAT_EQ(sprites.Sprites()[0].scale, 0.5f);
    EXPECT_FLOAT_EQ(joint.light.x, 0.49780244f);
    EXPECT_EQ(random.State(), expected.State());
}
