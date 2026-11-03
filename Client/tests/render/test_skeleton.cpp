#include "Render/Models/Skeleton.h"

#include "Data/Models/Model.h"
#include "Render/Models/HalfLifeMath.h"
#include "support/SyntheticModels.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

// Client::Render::Models::MakeSkeleton: el esqueleto como lo deja 0x4f13d0, sobre el modelo
// sintético de support/SyntheticModels.h (una raíz, un hijo y un dummy; dos acciones).
namespace
{
using Client::Data::Models::Model;
using Client::Render::Models::AngleQuaternion;
using Client::Render::Models::BoneKey;
using Client::Render::Models::MakeSkeleton;
using Client::Render::Models::Skeleton;
using Client::Tests::SampleModel;

glm::vec3 ToVec3(const std::array<float, 3>& value)
{
    return {value[0], value[1], value[2]};
}

// 0x4f13d0 calcula el cuaternión de cada key con 0x5fcd20, y guarda la posición y los ángulos.
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

// Los padres y los dummy, como vienen. Un dummy no tiene keys.
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

// Cada acción con sus keys y su lock_positions; +0 en 0 (repite) y sin velocidad, como 0x4f13d0, y
// sin hueso de la cabeza, como 0x4f1f20(0).
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

// Un padre que no es −1 ni un hueso de antes: el mismo hueso, uno de después (Q-C106), uno que no
// existe o un negativo. El error dice cuál.
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
