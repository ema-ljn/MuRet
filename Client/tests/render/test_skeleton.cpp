#include "Models/Skeleton.h"

#include "Data/Models/Model.h"
#include "Data/Models/PlayerActions.h"
#include "Data/Models/PlayerModels.h"
#include "Math/HalfLifeMath.h"
#include "support/SyntheticModels.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

// The skeleton as 0x4f13d0 leaves it, on the synthetic model of support/SyntheticModels.h (a root, a child and a
// dummy; two actions).
namespace
{
using Client::Data::Models::Action;
using Client::Data::Models::Bone;
using Client::Data::Models::Model;
using Client::Data::Models::PlayerActionSpeed;
using Client::Data::Models::PlayerActionsWithoutRepeat;
using Client::Data::Models::PlayerHeadBone;
using Client::Math::AngleQuaternion;
using Client::Models::BoneKey;
using Client::Models::MakePlayerSkeleton;
using Client::Models::MakeSkeleton;
using Client::Models::Skeleton;
using Client::Tests::SampleModel;

glm::vec3 ToVec3(const std::array<float, 3>& value)
{
    return {value[0], value[1], value[2]};
}

// 0x4f13d0 computes each key's quaternion with 0x5fcd20, and stores the position and the angles.
TEST(SkeletonTest, EveryKeyHasItsQuaternion)
{
    const Model model = SampleModel();

    const auto skeleton = MakeSkeleton(model);

    ASSERT_TRUE(skeleton.HasValue());
    ASSERT_EQ(skeleton.Value().bones.size(), model.bones.size());
    for (std::size_t bone = 0; bone < 2; ++bone)
    {
        const auto& actions = skeleton.Value().bones[bone].actions;
        ASSERT_EQ(actions.size(), model.actions.size());
        for (std::size_t action = 0; action < actions.size(); ++action)
        {
            const auto& keys = model.bones[bone].actions[action];
            ASSERT_EQ(actions[action].size(), keys.positions.size());
            for (std::size_t key = 0; key < actions[action].size(); ++key)
            {
                const BoneKey& made = actions[action][key];
                EXPECT_EQ(made.position, ToVec3(keys.positions[key]));
                EXPECT_EQ(made.angles, ToVec3(keys.rotations[key]));
                EXPECT_EQ(made.rotation, AngleQuaternion(ToVec3(keys.rotations[key])));
            }
        }
    }
}

// Parents and dummies, as they come. A dummy has no keys.
TEST(SkeletonTest, KeepsTheParentsAndTheDummies)
{
    const auto skeleton = MakeSkeleton(SampleModel());

    ASSERT_TRUE(skeleton.HasValue());
    const Skeleton& made = skeleton.Value();
    EXPECT_FALSE(made.bones[0].dummy);
    EXPECT_EQ(made.bones[0].parent, -1);
    EXPECT_EQ(made.bones[1].parent, 0);
    EXPECT_TRUE(made.bones[2].dummy);
    EXPECT_TRUE(made.bones[2].actions.empty());
}

// Each action with its keys and its lock_positions; +0 at 0 (repeats) and no speed, like 0x4f13d0, and no head bone,
// like 0x4f1f20(0).
TEST(SkeletonTest, TheActionsAreThoseOfTheReader)
{
    const auto skeleton = MakeSkeleton(SampleModel());

    ASSERT_TRUE(skeleton.HasValue());
    const Skeleton& made = skeleton.Value();
    ASSERT_EQ(made.actions.size(), 2u);
    EXPECT_EQ(made.actions[0].keyCount, 2);
    EXPECT_FALSE(made.actions[0].lockPositions);
    EXPECT_EQ(made.actions[1].keyCount, 3);
    EXPECT_TRUE(made.actions[1].lockPositions);
    for (const auto& action : made.actions)
    {
        EXPECT_TRUE(action.repeats);
        EXPECT_EQ(action.speed, 0.0f);
    }
    EXPECT_EQ(made.headBone, -1);
}

// A parent that is neither −1 nor an earlier bone: the same bone, a later one (Q-C108), one that does not exist or a
// negative one. The error says which.
TEST(SkeletonTest, AParentAfterItsChildIsAnError)
{
    for (const int parent : {1, 2, 7, -2})
    {
        Model model = SampleModel();
        model.bones[1].parent = static_cast<std::int16_t>(parent);

        const auto skeleton = MakeSkeleton(model);

        ASSERT_FALSE(skeleton.HasValue()) << parent;
        EXPECT_EQ(skeleton.Error().bone, 1u) << parent;
        EXPECT_EQ(skeleton.Error().parent, parent);
    }
}

// A model with the 205 actions of Player.bmd, two keys each, and one bone.
Model PlayerLikeModel(std::size_t actions)
{
    Model model;
    model.actions.assign(actions, Action{2, false, {}});
    Bone bone;
    bone.name = "Bip01";
    bone.actions.assign(actions, Client::Tests::SampleKeys(0, 0, 2));
    model.bones = {bone};
    return model;
}

// 0x60dbe0: each action's speed, from the last write that touches it; the 57 without any stay at 0 (Q-C87). The four
// with +0 at 1 do not repeat.
TEST(SkeletonTest, MakePlayerSkeletonSetsTheSpeeds)
{
    const auto skeleton = MakePlayerSkeleton(PlayerLikeModel(205));

    ASSERT_TRUE(skeleton.HasValue());
    const auto& actions = skeleton.Value().actions;
    ASSERT_EQ(actions.size(), 205u);
    for (std::size_t action = 0; action < actions.size(); ++action)
    {
        EXPECT_EQ(actions[action].speed, PlayerActionSpeed(action).value_or(0.0f)) << action;
    }
    EXPECT_EQ(actions[3].speed, 0.26f);
    EXPECT_EQ(actions[1].speed, 0.28f);
    EXPECT_EQ(actions[0].speed, 0.0f);
    for (const std::uint8_t action : PlayerActionsWithoutRepeat)
    {
        EXPECT_FALSE(actions[action].repeats) << static_cast<int>(action);
    }
    EXPECT_TRUE(actions[67].repeats);
}

// The character's head is bone 20 (0x60f301); any other model's, none.
TEST(SkeletonTest, ThePlayerHeadIsBoneTwenty)
{
    const auto player = MakePlayerSkeleton(PlayerLikeModel(205));
    const auto other = MakeSkeleton(PlayerLikeModel(205));

    ASSERT_TRUE(player.HasValue() && other.HasValue());
    EXPECT_EQ(player.Value().headBone, PlayerHeadBone);
    EXPECT_EQ(player.Value().headBone, 20);
    EXPECT_EQ(other.Value().headBone, -1);
}

TEST(SkeletonTest, MakePlayerSkeletonSetsWhatTheModelHas)
{
    const auto skeleton = MakePlayerSkeleton(PlayerLikeModel(4));

    ASSERT_TRUE(skeleton.HasValue());
    ASSERT_EQ(skeleton.Value().actions.size(), 4u);
    EXPECT_EQ(skeleton.Value().actions[3].speed, 0.26f);
}

TEST(SkeletonTest, MakePlayerSkeletonKeepsTheErrors)
{
    Model model = SampleModel();
    model.bones[1].parent = 2;

    EXPECT_FALSE(MakePlayerSkeleton(model).HasValue());
}
} // namespace
