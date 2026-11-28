#include "Actors/CharacterAnimation.h"

#include "Kernel/LoopTiming.h"
#include "Tables/PlayerModels.h"
#include "Tables/PlayerActions.h"
#include "Kernel/CharacterAppearance.h"
#include "Models/Animation.h"
#include "Models/IModelLibrary.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Actors/PlayerActionSpeeds.h"

#include <cstdint>

namespace Client::Actors
{
namespace
{
constexpr std::uint8_t FastWingAction = Tables::FirstFlyingAction;
constexpr std::uint8_t OtherFastWingAction = Tables::SecondFlyingAction;
constexpr float FastWingKeys = 1.0f;
constexpr float WingKeys = 0.25f;
} // namespace

float WingKeysPerOriginalFrame(std::uint8_t characterAction)
{
    return characterAction == FastWingAction || characterAction == OtherFastWingAction ? FastWingKeys : WingKeys;
}

bool StepCharacterAnimation(CharacterAnimation& animation, const Kernel::CharacterAppearance& appearance,
                            const Models::IModelLibrary& models, Models::AnimationScene scene)
{
    bool finished = false;
    const Models::Skeleton* skeleton = models.SkeletonOf(Tables::PlayerSkeletonModelId);
    if (skeleton != nullptr && animation.body.action < skeleton->actions.size())
    {
        const Models::ActionInfo& action = skeleton->actions[animation.body.action];
        finished = Models::AdvanceAnimation(animation.body, action, action.speed, scene);
    }
    StepCharacterAttachments(animation, appearance, models);
    return finished;
}

void StepCharacterAttachments(CharacterAnimation& animation, const Kernel::CharacterAppearance& appearance,
                              const Models::IModelLibrary& models)
{
    if (appearance.pet)
    {
        const Models::Skeleton* pet = models.SkeletonOf(Models::ModelId{*appearance.pet});
        if (pet != nullptr && !pet->actions.empty())
        {
            Models::AdvanceByTime(animation.pet, pet->actions[0], Models::PetKeysPerOriginalFrame, Kernel::LogicStep);
        }
    }
    if (appearance.wings)
    {
        const Models::Skeleton* wings = models.SkeletonOf(Models::ModelId{*appearance.wings});
        if (wings != nullptr && !wings->actions.empty())
        {
            Models::AdvanceByTime(animation.wings, wings->actions[0], WingKeysPerOriginalFrame(animation.body.action),
                                  Kernel::LogicStep);
        }
    }
}

Models::AnimationState BodyBetweenSteps(const CharacterAnimation& animation, const Models::IModelLibrary& models,
                                        float alpha, const PlayerActionSpeeds* speeds)
{
    const Models::Skeleton* skeleton = models.SkeletonOf(Tables::PlayerSkeletonModelId);
    if (skeleton == nullptr || animation.body.action >= skeleton->actions.size())
    {
        return animation.body;
    }
    const Models::ActionInfo& action = skeleton->actions[animation.body.action];
    const float speed = speeds != nullptr ? speeds->Speed(animation.body.action, action.speed) : action.speed;
    return Models::Interpolated(animation.body, action, speed, alpha);
}
} // namespace Client::Actors
