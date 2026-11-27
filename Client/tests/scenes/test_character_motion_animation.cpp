#include "Kernel/CharacterAppearance.h"
#include "Models/IModelLibrary.h"
#include "Actors/CharacterAnimation.h"

#include "Data/Models/Model.h"
#include "Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Actors/PlayerActionSpeeds.h"
#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace
{
class WalkingModels : public Client::Models::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    const Client::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return &skeleton;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
    Client::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
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

// 0x50ba30
TEST(CharacterMotionAnimationTest, TheWingsRunFasterInThirtyAndThirtyOne)
{
    std::array<float, 256> expected;
    expected.fill(0.25f);
    expected[30] = 1.0f;
    expected[31] = 1.0f;

    for (std::size_t action = 0; action < expected.size(); ++action)
    {
        EXPECT_EQ(Client::Scenes::WingKeysPerOriginalFrame(static_cast<std::uint8_t>(action)), expected[action])
            << action;
    }
}

TEST(CharacterMotionAnimationTest, AttachmentsAdvanceWingsAtTheRateOfTheBodyAction)
{
    WalkingModels models;
    models.skeleton.actions[0] = {.repeats = true, .speed = 0.8f, .keyCount = 20};
    Client::GameLogic::CharacterAppearance appearance;
    appearance.wings = std::uint16_t{0x1a12};
    Client::Scenes::CharacterAnimation animation;
    animation.body.action = 30;

    Client::Scenes::StepCharacterAttachments(animation, appearance, models);
    Client::Scenes::StepCharacterAttachments(animation, appearance, models);

    EXPECT_FLOAT_EQ(animation.wings.state.frame, 1.0f);
}
