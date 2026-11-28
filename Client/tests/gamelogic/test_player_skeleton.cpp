#include "Assets/PlayerSkeleton.h"

#include "Data/Models/Model.h"
#include "Tables/PlayerActions.h"
#include "Tables/PlayerBones.h"
#include "Models/Skeleton.h"
#include "support/SyntheticModels.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

namespace
{
using Client::Assets::MakePlayerSkeleton;
using Client::Data::Models::Action;
using Client::Data::Models::Bone;
using Client::Data::Models::Model;
using Client::Models::MakeSkeleton;
using Client::Tables::PlayerActionSpeed;
using Client::Tables::PlayerActionsWithoutRepeat;
using Client::Tables::PlayerHeadBone;
using Client::Tests::SampleModel;

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
TEST(PlayerSkeletonTest, SetsTheSpeeds)
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
TEST(PlayerSkeletonTest, ThePlayerHeadIsBoneTwenty)
{
    const auto player = MakePlayerSkeleton(PlayerLikeModel(205));
    const auto other = MakeSkeleton(PlayerLikeModel(205));

    ASSERT_TRUE(player.HasValue() && other.HasValue());
    EXPECT_EQ(player.Value().headBone, PlayerHeadBone);
    EXPECT_EQ(player.Value().headBone, 20);
    EXPECT_EQ(other.Value().headBone, -1);
}

TEST(PlayerSkeletonTest, SetsWhatTheModelHas)
{
    const auto skeleton = MakePlayerSkeleton(PlayerLikeModel(4));

    ASSERT_TRUE(skeleton.HasValue());
    ASSERT_EQ(skeleton.Value().actions.size(), 4u);
    EXPECT_EQ(skeleton.Value().actions[3].speed, 0.26f);
}

TEST(PlayerSkeletonTest, KeepsTheErrors)
{
    Model model = SampleModel();
    model.bones[1].parent = 2;

    EXPECT_FALSE(MakePlayerSkeleton(model).HasValue());
}
} // namespace
