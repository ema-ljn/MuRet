#include "Scenes/CharacterAnimation.h"

#include "App/LoopTiming.h"
#include "Data/Models/PlayerModels.h"
#include "GameLogic/CharacterAppearance.h"
#include "Models/Animation.h"
#include "Models/IModelLibrary.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Scenes/PlayerActionSpeeds.h"

#include <cstdint>

namespace Client::Scenes
{
namespace
{
constexpr std::uint8_t FastWingAction = 0x1e;
constexpr std::uint8_t OtherFastWingAction = 0x1f;
constexpr float FastWingKeys = 1.0f;
constexpr float WingKeys = 0.25f;
} // namespace

float WingKeysPerOriginalFrame(std::uint8_t characterAction)
{
    return characterAction == FastWingAction || characterAction == OtherFastWingAction ? FastWingKeys : WingKeys;
}

bool StepCharacterAnimation(CharacterAnimation& animation, const GameLogic::CharacterAppearance& appearance,
                            const Render::Models::IModelLibrary& models, Render::Models::AnimationScene scene)
{
    bool finished = false;
    const Render::Models::Skeleton* skeleton = models.SkeletonOf(Data::Models::PlayerSkeletonModel);
    if (skeleton != nullptr && animation.body.action < skeleton->actions.size())
    {
        const Render::Models::ActionInfo& action = skeleton->actions[animation.body.action];
        finished = Render::Models::AdvanceAnimation(animation.body, action, action.speed, scene);
    }
    StepCharacterAttachments(animation, appearance, models);
    return finished;
}

void StepCharacterAttachments(CharacterAnimation& animation, const GameLogic::CharacterAppearance& appearance,
                              const Render::Models::IModelLibrary& models)
{
    if (appearance.pet)
    {
        const Render::Models::Skeleton* pet = models.SkeletonOf(*appearance.pet);
        if (pet != nullptr && !pet->actions.empty())
        {
            Render::Models::AdvanceByTime(animation.pet, pet->actions[0], Render::Models::PetKeysPerOriginalFrame,
                                          App::LogicStep);
        }
    }
    if (appearance.wings)
    {
        const Render::Models::Skeleton* wings = models.SkeletonOf(*appearance.wings);
        if (wings != nullptr && !wings->actions.empty())
        {
            Render::Models::AdvanceByTime(animation.wings, wings->actions[0],
                                          WingKeysPerOriginalFrame(animation.body.action), App::LogicStep);
        }
    }
}

Render::Models::AnimationState BodyBetweenSteps(const CharacterAnimation& animation,
                                                const Render::Models::IModelLibrary& models, float alpha,
                                                const PlayerActionSpeeds* speeds)
{
    const Render::Models::Skeleton* skeleton = models.SkeletonOf(Data::Models::PlayerSkeletonModel);
    if (skeleton == nullptr || animation.body.action >= skeleton->actions.size())
    {
        return animation.body;
    }
    const Render::Models::ActionInfo& action = skeleton->actions[animation.body.action];
    const float speed = speeds != nullptr ? speeds->Speed(animation.body.action, action.speed) : action.speed;
    return Render::Models::Interpolated(animation.body, action, speed, alpha);
}
} // namespace Client::Scenes
