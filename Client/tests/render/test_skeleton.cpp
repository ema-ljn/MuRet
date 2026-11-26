#include "Models/Skeleton.h"

#include "Data/Models/Model.h"
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
using Client::Math::AngleQuaternion;
using Client::Models::BoneKey;
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

} // namespace
