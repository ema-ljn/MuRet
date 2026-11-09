#include "Scenes/CharacterAnimation.h"

#include "App/LoopTiming.h"
#include "Data/Models/PlayerModels.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/PlayerActionSpeeds.h"

namespace Client::Scenes
{
bool StepCharacterAnimation(CharacterAnimation& animation, const GameLogic::CharacterAppearance& appearance,
                            const IModelLibrary& models, Render::Models::AnimationScene scene,
                            const PlayerActionSpeeds* speeds)
{
    bool finished = false;
    const Render::Models::Skeleton* skeleton = models.SkeletonOf(Data::Models::PlayerSkeletonModel);
    if (skeleton != nullptr && animation.body.action < skeleton->actions.size())
    {
        const Render::Models::ActionInfo& action = skeleton->actions[animation.body.action];
        finished = Render::Models::AdvanceAnimation(
            animation.body, action,
            speeds != nullptr ? (*speeds)[animation.body.action].value_or(action.speed) : action.speed, scene);
    }
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
                                          Render::Models::WingKeysPerOriginalFrame(animation.body.action),
                                          App::LogicStep);
        }
    }
    return finished;
}

Render::Models::AnimationState BodyBetweenSteps(const CharacterAnimation& animation, const IModelLibrary& models,
                                                float alpha, const PlayerActionSpeeds* speeds)
{
    const Render::Models::Skeleton* skeleton = models.SkeletonOf(Data::Models::PlayerSkeletonModel);
    if (skeleton == nullptr || animation.body.action >= skeleton->actions.size())
    {
        return animation.body;
    }
    const Render::Models::ActionInfo& action = skeleton->actions[animation.body.action];
    return Render::Models::Interpolated(
        animation.body, action,
        speeds != nullptr ? (*speeds)[animation.body.action].value_or(action.speed) : action.speed, alpha);
}
} // namespace Client::Scenes
