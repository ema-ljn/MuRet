#pragma once

#include "Core/Expected.h"
#include "Particles/ParticleOwner.h"

#include <span>
#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::World
{
struct ModelEffect;
class Particles;
} // namespace Client::World

namespace Client::Actors
{
class IJointHooks;
} // namespace Client::Actors

// The step (0x5208a0) of the model effects a character owns and whose step reads it live: the full set's 0x5ac/3, the
// 0x5ac/1 and /2 of the skills that arrive and the colored decal 0x61e of the pet and of the summons. Their joints are
// born through the hooks (Actors/JointHooks.h).
namespace Client::Scenes
{
struct JointState;

// 0x5208a0 on the full set's 0x5ac/3 (0x52a9ad): the light's rand() for every live effect (0x5208c5), +0xd8 one step
// more and, with the life a multiple of 15 and an owner, the z turn 180 more; the position at 50 from the owner
// character (+0x108, read live) with the unrounded sine of the turn and the cosine of the stored turn (Q-C380), 20
// higher, and the 0x619/9 joint there. Then the common tail (0x52cb98): no animation or speed, one life less.
[[nodiscard]] Core::Expected<void, std::string> StepFullSetFlare(World::ModelEffect& effect,
                                                                 std::span<JointState> joints,
                                                                 const Actors::IJointHooks* jointHooks,
                                                                 Crt::CrtRandom& random);

// 0x5208a0 on the 0x61e/0 decal of the level-up, /1 of the pet and /3 of summoning. /0 only consumes the common rand()
// and takes off a life (0x52b984 → 0x52bb29). /1 (0x52b9a6 → 0x518900): the light's rand() for every live effect
// (0x5208c5) and three 0x619/1 joints of width 5, each with its turn (rand() % 90, 0, rand() % 360), born where that
// turn takes (0, −200, 0) subtracted from the effect's position, with 120 more z; the effect's position as the second
// point, the effect's owner (the hero), key −1 and no skill or target. /3 of the 0x1c and of the summons does the same
// with 0x619/3 joints (0x5189cc), with the decal's character as owner. /2, /5 and those from 0xb on also go to
// 0x518900, with 0x619 joints of the same subtype except /5, which asks for 0x619/0xb (0x51898b). Negative ones, like
// /0. Then the common tail (0x52cb98): no animation or speed, one life less. /4 and /6 to /0xa belong to
// Scenes/HighModelEffects (StepsAsPetSummon leaves them out).
[[nodiscard]] bool StepsAsPetSummon(const World::ModelEffect& effect);
[[nodiscard]] Core::Expected<void, std::string> StepPetSummon(World::ModelEffect& effect, std::span<JointState> joints,
                                                              const Actors::IJointHooks* jointHooks,
                                                              Crt::CrtRandom& random);

// 0x5208a0 on the 0x5ac/1 and /2 of the skills that arrive (0x52a7b7 and 0x52a8a7, Scenes/SkillReplies): the light's
// rand() for every live effect (0x5208c5) and the life at 10 with the owner (+0x108) alive (+0x0 at 1) and its flag of
// +0x78 (0x10 for /1, 0x400 for /2), otherwise at 0. With 0x4000 in the owner's +0x78, nothing more. Otherwise /1 draws
// rand() % 7 and creates its two 0x5ac/4 particles (0x52a866/0x52a89a) with `particleOwner`, the owner's identity in
// the pool; /2 raises +0xd8 by 1 and, with its ftol a multiple of 50, turns 50 more in z, places itself at 80 from the
// owner with the unrounded sine of the turn and the cosine of the stored turn (Q-C380), 50 higher, and releases the
// 0x619/9 joint of width 15 there (with the joint table full, none). Then the common tail (0x52cb98): no animation or
// speed, one life less. Without an owner, the original reads +0x78 of 0: MuRet stops.
[[nodiscard]] Core::Expected<void, std::string>
StepSkillFlare(World::ModelEffect& effect, std::span<JointState> joints, const Actors::IJointHooks* jointHooks,
               Crt::CrtRandom& random, World::Particles* particles = nullptr, World::ParticleOwner particleOwner = {});
} // namespace Client::Scenes
