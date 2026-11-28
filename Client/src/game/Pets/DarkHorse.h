#pragma once

#include "Assets/ModelIds.h"
#include "Kernel/HelperTable.h"
#include "Models/Animation.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Actors/Drawing/CharacterDrawing.h"

#include <cstdint>

// The Dark Horse: helper 0xbf (Skill\DarkHorse.bmd) a character rides with pet 0x1c16. 0x443d70 creates it at the
// rider's place, 0x444010 moves it each step and 0x445260 → 0x602e90 draws it
// (Client/docs/research/06-render-escenas-y-ui.md, "El caballo oscuro"). The rider, dust, breath and effects are in
// Pets/HelperTable.h. Without the rider's head at its bone 7 (in DarkHorse.bmd, a root no vertex uses).
namespace Client::Pets
{
inline constexpr std::uint16_t DarkHorseModel = Assets::DarkHorseModel;
// 0x445260's scale with the rider in the login (scene 2); in the selection (4) and the game (5), 1.
inline constexpr float DarkHorseLoginScale = 1.6f;

// The horse's action and speed for the rider's action (+0x111) (0x444010 with 0xbf).
struct DarkHorseGait
{
    std::uint8_t action = 0;
    float speed = 0.0f;
};

[[nodiscard]] DarkHorseGait DarkHorseGaitOf(std::uint8_t riderAction);

// 0x4e9980 with +0x16d at 0 (0x443d70): alpha rises toward +0x170 (1, from 0x443d70) by a tenth of what it lacks, with
// a single rounding to float. Runs at the start of the step, also on the safe cell.
void FadeInDarkHorse(Kernel::DarkHorse& horse);

// 0x444145..0x444535: the action from the rider's (0x4ea030) and its speed in +0xd8; with a rider action that is none
// of the horse's, also the earthquake count (+0x8e) to 0 (0x44452e).
void TakeTheRidersGait(Kernel::DarkHorse& horse, const Models::Skeleton& skeleton, std::uint8_t riderAction);

// 0x445006..0x445039: advances its action at +0xd8 (0x4ec200), the end of the step, the other helpers' too. In scene 4
// (`scene`) it decides the end with frame + 2 (Q-C21).
void AdvanceDarkHorse(Kernel::DarkHorse& horse, const Models::Skeleton& skeleton,
                      Models::AnimationScene scene = Models::AnimationScene::Other);

// A 0x444010 step with 0xbf, without what makes effects. On the rider's safe cell (0x4440b8..0x4440f8) alpha goes back
// to 0 and the horse advances with the speed it had, ignoring the rider's action.
void StepDarkHorse(Kernel::DarkHorse& horse, const Models::Skeleton& skeleton, std::uint8_t riderAction,
                   bool riderInSafeCell);

// The login step (scene 2): world 56 has no cell with attribute bit 0 (06, "El caballo oscuro"), so the rider's cell
// never hides it.
void StepDarkHorse(Kernel::DarkHorse& horse, const Models::Skeleton& skeleton, std::uint8_t riderAction);

// The horse drawn between two steps (`stepAlpha` of one step more), at `place` (the rider's place and angles in the
// login, its own in the game) with 0x445260's scale: white, with per-vertex light and its alpha (DrawDarkHorse).
[[nodiscard]] Actors::Drawing::DrawnObject DrawnDarkHorse(const Kernel::DarkHorse& horse,
                                                          const Models::Skeleton* skeleton,
                                                          const Models::ObjectTransform& place, float scale,
                                                          float stepAlpha);
} // namespace Client::Pets
