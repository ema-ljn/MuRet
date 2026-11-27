#pragma once

#include "Kernel/CharacterAppearance.h"
#include "Models/Animation.h"
#include "Models/IModelLibrary.h"
#include "Models/Pose.h"

#include <cstdint>

// How much a character advances on each logic step (Client/docs/research/06-render-escenas-y-ui.md,
// "Cuánto avanza la animación"): the body at its action's speed, and the pet and the wings by
// time (Q-C20). The selection and the login share it.
namespace Client::Actors
{
class PlayerActionSpeeds;
struct CharacterAnimation
{
    Models::AnimationState body;
    Models::TimedAnimation pet;
    Models::TimedAnimation wings;
};

// 0x50ba30: the body action selects the wing speed (+0x308 in the part at +0x2f8).
[[nodiscard]] float WingKeysPerOriginalFrame(std::uint8_t characterAction);

// One step: 0x4ec200 with the Player.bmd body at its action's speed (with the rule of `scene`:
// the selection decides "finished" with the frame + 2, Q-C21), and the pet and the wings one time step,
// the wings at the speed 0x50ba30 sets in +0x308 by the body's action. What the
// library does not have does not advance. Returns whether the body's action finished: with that, 0x4fcd00 sets
// the standing one again (0x4fcb10 → 0x4f2250).
[[nodiscard]] bool StepCharacterAnimation(CharacterAnimation& animation, const Kernel::CharacterAppearance& appearance,
                                          const Models::IModelLibrary& models, Models::AnimationScene scene);

// Only the two hanging animations: the body already came from the ActorState that 0x508f00 advanced.
void StepCharacterAttachments(CharacterAnimation& animation, const Kernel::CharacterAppearance& appearance,
                              const Models::IModelLibrary& models);

// The body posed between two steps: `alpha` of one more step (Models::Interpolated). Without the
// action in Player.bmd, the step's.
[[nodiscard]] Models::AnimationState BodyBetweenSteps(const CharacterAnimation& animation,
                                                      const Models::IModelLibrary& models, float alpha,
                                                      const PlayerActionSpeeds* speeds = nullptr);
} // namespace Client::Actors
