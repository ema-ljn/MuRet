#include "Scenes/DarkHorse.h"

#include "Render/Models/Animation.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/CharacterDrawing.h"

#include <cstdint>

namespace Client::Scenes
{
namespace
{
// Las velocidades que 0x444010 escribe en +0xd8.
constexpr float SlowGait = 0.34f; // 0x3eae147b
constexpr float Gesture = 1.0f;   // 0x3f800000
constexpr float Standing = 0.3f;  // 0x3e99999a
// 0x4e9980 con +0x16d en 0: el alpha va hacia +0x170 (1, de 0x443d70) un décimo (0x6b87b8) por paso.
constexpr float AlphaGoal = 1.0f;
constexpr float AlphaRate = 0.1f;
// 0x4440f2: en la celda segura del jinete, +0x174 = 0 (el EBX de 0x44401e).
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

void FadeInDarkHorse(DarkHorse& horse)
{
    // En la x87 (0x4e9a0c..0x4e9a1e): un solo redondeo a float, al final.
    horse.alpha = static_cast<float>(
        ((static_cast<double>(AlphaGoal) - static_cast<double>(horse.alpha)) * static_cast<double>(AlphaRate)) +
        static_cast<double>(horse.alpha));
}

void TakeTheRidersGait(DarkHorse& horse, const Render::Models::Skeleton& skeleton, std::uint8_t riderAction)
{
    const DarkHorseGait gait = DarkHorseGaitOf(riderAction);
    Render::Models::SetAction(horse.animation, skeleton, gait.action);
    horse.speed = gait.speed;
}

void AdvanceDarkHorse(DarkHorse& horse, const Render::Models::Skeleton& skeleton)
{
    if (horse.animation.action < skeleton.actions.size())
    {
        static_cast<void>(Render::Models::AdvanceAnimation(horse.animation, skeleton.actions[horse.animation.action],
                                                           horse.speed, Render::Models::AnimationScene::Other));
    }
}

void StepDarkHorse(DarkHorse& horse, const Render::Models::Skeleton& skeleton, std::uint8_t riderAction,
                   bool riderInSafeCell)
{
    FadeInDarkHorse(horse);
    if (riderInSafeCell)
    {
        // 0x4440f2..0x4440f8: el alpha en 0 y al avance, sin tocar la acción ni +0xd8.
        horse.alpha = HiddenAlpha;
    }
    else
    {
        TakeTheRidersGait(horse, skeleton, riderAction);
    }
    AdvanceDarkHorse(horse, skeleton);
}

void StepDarkHorse(DarkHorse& horse, const Render::Models::Skeleton& skeleton, std::uint8_t riderAction)
{
    StepDarkHorse(horse, skeleton, riderAction, false);
}

DrawnObject DrawnDarkHorse(const DarkHorse& horse, const Render::Models::Skeleton* skeleton,
                           const Render::Models::ObjectTransform& place, float scale, float stepAlpha)
{
    DrawnObject drawn;
    drawn.model = DarkHorseModel;
    drawn.animation = horse.animation;
    if (skeleton != nullptr && horse.animation.action < skeleton->actions.size())
    {
        drawn.animation = Render::Models::Interpolated(horse.animation, skeleton->actions[horse.animation.action],
                                                       horse.speed, stepAlpha);
    }
    drawn.transform = place;
    drawn.transform.scale = scale;
    drawn.alpha = horse.alpha;
    drawn.vertexLight = true;
    return drawn;
}
} // namespace Client::Scenes
