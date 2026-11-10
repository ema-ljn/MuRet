#include "Scenes/CharacterAnimation.h"

#include "Data/Models/Model.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/PlayerActionSpeeds.h"
#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>

namespace
{
class WalkingModels : public Client::Scenes::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return &skeleton;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
    Client::Render::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
};
} // namespace

TEST(CharacterMotionAnimationTest, BetweenStepsUsesDurableSharedSpeedWithoutChangingTheResource)
{
    WalkingModels models;
    models.skeleton.actions[13] = {.repeats = true, .speed = 0.6f, .keyCount = 20};
    Client::Scenes::CharacterAnimation animation;
    animation.body.action = 13;
    animation.body.frame = 1.0f;
    Client::Scenes::PlayerActionSpeeds speeds;
    speeds.SetWalking(true);
    const auto slowed = Client::Scenes::BodyBetweenSteps(animation, models, 0.5f, &speeds);
    EXPECT_FLOAT_EQ(slowed.frame, 1.0825f);
    EXPECT_FLOAT_EQ(animation.body.frame, 1.0f);
    EXPECT_FLOAT_EQ(models.skeleton.actions[13].speed, 0.6f);
    speeds.SetWalking(false);
    const auto running = Client::Scenes::BodyBetweenSteps(animation, models, 0.5f, &speeds);
    EXPECT_FLOAT_EQ(running.frame, 1.165f);
}
