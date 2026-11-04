#include "Render/Models/Animation.h"

#include "Core/IClock.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace Client::Render::Models
{
namespace
{
// Los floats de 0x4ec200: 0 (0x6b86a0), 1 (0x6b87d4), el + 2 de la selección (0x6b8818) y el 0,01 de
// una acción que no repite (0x6b88d4).
constexpr float Zero = 0.0f;
constexpr float One = 1.0f;
constexpr float SelectionLookAhead = 2.0f;
constexpr float HoldBeforeTheEnd = 0.01f;

// Las acciones en que las alas van a 1 key por frame (0x50ba30) y lo que van en las otras.
constexpr std::uint8_t FastWingAction = 0x1e;
constexpr std::uint8_t OtherFastWingAction = 0x1f;
constexpr float FastWingKeys = 1.0f;
constexpr float WingKeys = 0.25f;

// El __ftol del original: trunca hacia 0. Fuera de lo que entra en un int, el original da los 32 bits
// de abajo de su entero de 64 (FISTP qword); acá el int más chico. Un frame no llega ahí.
int Truncate(float value)
{
    const float whole = std::trunc(value);
    if (!(whole > static_cast<float>(std::numeric_limits<int>::min()) &&
          whole < static_cast<float>(std::numeric_limits<int>::max())))
    {
        return std::numeric_limits<int>::min();
    }
    return static_cast<int>(whole);
}
} // namespace

bool AdvanceAnimation(AnimationState& state, const ActionInfo& action, float speed, AnimationScene scene)
{
    if (action.keyCount <= 1)
    {
        return false;
    }
    const int before = Truncate(state.frame);
    state.frame += speed;
    if (Truncate(state.frame) != before)
    {
        state.previousAction = state.action;
        state.previousKey = static_cast<std::int16_t>(before);
    }
    if (state.frame <= Zero)
    {
        state.frame = (static_cast<float>(action.keyCount) - One) + state.frame;
    }

    if (!action.repeats)
    {
        if (static_cast<float>(action.keyCount) <= state.frame)
        {
            state.frame = static_cast<float>(action.keyCount) - HoldBeforeTheEnd;
            return true;
        }
        return false;
    }
    const int keys = action.lockPositions ? action.keyCount - 1 : action.keyCount;
    const float decided = scene == AnimationScene::CharacterSelect ? state.frame + SelectionLookAhead : state.frame;
    if (static_cast<float>(keys) <= decided)
    {
        const int whole = Truncate(state.frame);
        state.frame = (state.frame - static_cast<float>(whole)) + static_cast<float>(whole % keys);
        return true;
    }
    return false;
}

void SetAction(AnimationState& state, const Skeleton& skeleton, std::uint8_t action)
{
    if (action >= skeleton.actions.size() || action == state.action)
    {
        return;
    }
    state.previousAction = state.action;
    state.previousKey = static_cast<std::int16_t>(Truncate(state.frame));
    state.action = action;
    state.frame = Zero;
}

float WingKeysPerOriginalFrame(std::uint8_t characterAction)
{
    return characterAction == FastWingAction || characterAction == OtherFastWingAction ? FastWingKeys : WingKeys;
}

void AdvanceByTime(TimedAnimation& animation, const ActionInfo& action, float keysPerOriginalFrame,
                   Core::Duration elapsed)
{
    if (elapsed > Core::Duration::zero())
    {
        animation.carry += elapsed;
    }
    while (animation.carry >= OriginalFrameTime)
    {
        animation.carry -= OriginalFrameTime;
        (void)AdvanceAnimation(animation.state, action, keysPerOriginalFrame, AnimationScene::Other);
    }
}

AnimationState Interpolated(const AnimationState& state, const ActionInfo& action, float speed, float alpha)
{
    AnimationState copy = state;
    (void)AdvanceAnimation(copy, action, speed * alpha, AnimationScene::Other);
    return copy;
}
} // namespace Client::Render::Models
