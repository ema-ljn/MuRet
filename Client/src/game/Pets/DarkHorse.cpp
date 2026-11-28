#include "Pets/DarkHorse.h"
#include "Kernel/HelperTable.h"

#include "Models/Animation.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Actors/Drawing/CharacterDrawing.h"

#include <cstdint>

namespace Client::Pets
{
namespace
{
// The speeds 0x444010 writes to +0xd8.
constexpr float SlowGait = 0.34f; // 0x3eae147b
constexpr float Gesture = 1.0f;   // 0x3f800000
constexpr float Standing = 0.3f;  // 0x3e99999a
// 0x4e9980 with +0x16d at 0: alpha moves toward +0x170 (1, from 0x443d70) by a tenth (0x6b87b8) per step.
constexpr float AlphaGoal = 1.0f;
constexpr float AlphaRate = 0.1f;
// 0x4440f2: on the rider's safe cell, +0x174 = 0 (the EBX of 0x44401e).
constexpr float HiddenAlpha = 0.0f;
} // namespace

DarkHorseGait DarkHorseGaitOf(std::uint8_t riderAction)
{
    switch (riderAction)
    {
    case 0x53:
        return {3, SlowGait};
    case 0x4b:
        return {1, SlowGait};
    case 0x4e:
    case 0x4f:
    case 0x50:
    case 0x51:
    case 0x52:
        return {2, SlowGait};
    case 0x54:
        return {5, Gesture};
    case 0x55:
        return {6, Gesture};
    default:
        return {0, Standing};
    }
}

void FadeInDarkHorse(Kernel::DarkHorse& horse)
{
    // On the x87 (0x4e9a0c..0x4e9a1e): a single rounding to float, at the end.
    horse.alpha = static_cast<float>(
        ((static_cast<double>(AlphaGoal) - static_cast<double>(horse.alpha)) * static_cast<double>(AlphaRate)) +
        static_cast<double>(horse.alpha));
}

void TakeTheRidersGait(Kernel::DarkHorse& horse, const Models::Skeleton& skeleton, std::uint8_t riderAction)
{
    const DarkHorseGait gait = DarkHorseGaitOf(riderAction);
    Models::SetAction(horse.animation, skeleton, gait.action);
    // 0x444523..0x44452e: only the other-actions branch (the only one that gives action 0) clears the earthquake count.
    if (gait.action == 0)
    {
        horse.effectCount = 0;
    }
    horse.speed = gait.speed;
}

void AdvanceDarkHorse(Kernel::DarkHorse& horse, const Models::Skeleton& skeleton, Models::AnimationScene scene)
{
    if (horse.animation.action < skeleton.actions.size())
    {
        static_cast<void>(
            Models::AdvanceAnimation(horse.animation, skeleton.actions[horse.animation.action], horse.speed, scene));
    }
}

void StepDarkHorse(Kernel::DarkHorse& horse, const Models::Skeleton& skeleton, std::uint8_t riderAction,
                   bool riderInSafeCell)
{
    FadeInDarkHorse(horse);
    if (riderInSafeCell)
    {
        // 0x4440f2..0x4440f8: alpha to 0, then straight to the advance, without touching the action or +0xd8.
        horse.alpha = HiddenAlpha;
    }
    else
    {
        TakeTheRidersGait(horse, skeleton, riderAction);
    }
    AdvanceDarkHorse(horse, skeleton);
}

void StepDarkHorse(Kernel::DarkHorse& horse, const Models::Skeleton& skeleton, std::uint8_t riderAction)
{
    StepDarkHorse(horse, skeleton, riderAction, false);
}

Actors::Drawing::DrawnObject DrawnDarkHorse(const Kernel::DarkHorse& horse, const Models::Skeleton* skeleton,
                                            const Models::ObjectTransform& place, float scale, float stepAlpha)
{
    Actors::Drawing::DrawnObject drawn;
    drawn.model = DarkHorseModel;
    drawn.animation = horse.animation;
    if (skeleton != nullptr && horse.animation.action < skeleton->actions.size())
    {
        drawn.animation =
            Models::Interpolated(horse.animation, skeleton->actions[horse.animation.action], horse.speed, stepAlpha);
    }
    drawn.transform = place;
    drawn.transform.scale = scale;
    drawn.alpha = horse.alpha;
    drawn.vertexLight = true;
    return drawn;
}
} // namespace Client::Pets
