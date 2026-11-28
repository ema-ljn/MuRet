#include "Assets/PlayerSkeleton.h"

#include "Data/Models/Model.h"
#include "Tables/PlayerActions.h"
#include "Tables/PlayerModels.h"
#include "Models/Skeleton.h"
#include "Tables/AddedEmotes.h"
#include "support/SyntheticModels.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

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

// MuRet's added emotes set the speed and the repeat of the actions the model has, and the others are left out.
TEST(PlayerSkeletonTest, AddedEmotesSetTheirActions)
{
    const std::vector<Client::Tables::AddedEmote> emotes{
        {.action = 205, .elfAction = 205, .keysPerStep = 0.5f, .repeats = false, .triggers = {"sit"}},
        {.action = 240, .elfAction = 240, .keysPerStep = 0.4f, .repeats = true, .triggers = {"far"}},
    };

    const auto skeleton = MakePlayerSkeleton(PlayerLikeModel(206), emotes);

    ASSERT_TRUE(skeleton.HasValue());
    ASSERT_EQ(skeleton.Value().actions.size(), 206u);
    EXPECT_EQ(skeleton.Value().actions[205].speed, 0.5f);
    EXPECT_FALSE(skeleton.Value().actions[205].repeats);
}

// An added elf action plays at its row's speed; an original one keeps the original's, and a row's own action its own.
TEST(PlayerSkeletonTest, AnAddedElfActionTakesItsRowsSpeed)
{
    const auto original = MakePlayerSkeleton(PlayerLikeModel(209));
    ASSERT_TRUE(original.HasValue());
    const std::vector<Client::Tables::AddedEmote> emotes{
        {.action = 205, .elfAction = 206, .keysPerStep = 0.5f, .repeats = false, .triggers = {"a"}},
        {.action = 207, .elfAction = 205, .keysPerStep = 0.25f, .repeats = true, .triggers = {"b"}},
        {.action = 208, .elfAction = 3, .keysPerStep = 0.25f, .repeats = true, .triggers = {"c"}},
    };

    const auto skeleton = MakePlayerSkeleton(PlayerLikeModel(209), emotes);

    ASSERT_TRUE(skeleton.HasValue());
    EXPECT_EQ(skeleton.Value().actions[206].speed, 0.5f);
    EXPECT_FALSE(skeleton.Value().actions[206].repeats);
    EXPECT_EQ(skeleton.Value().actions[205].speed, 0.5f);
    EXPECT_EQ(skeleton.Value().actions[3].speed, original.Value().actions[3].speed);
}
} // namespace
