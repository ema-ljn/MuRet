#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// The 0x114 (Skill\Wave) and the 0x115 (Skill\Piercing): 0x518cf0's creations (0x51c129 and 0x51c8e8) that arrive
// through the high types. The steps (0x526dd0 and 0x526f9d) are StepGameModelEffect's: they read nothing from outside
// except the effects table. 06, "Los efectos con modelo de los tipos altos".
namespace Client::Kernel
{
struct ModelEffectArguments;
} // namespace Client::Kernel

namespace Client::Effects
{
struct ModelEffect;
} // namespace Client::Effects

namespace Client::Effects
{
struct HighEffectContext;

inline constexpr std::int16_t WaveEffect = 0x114;
inline constexpr std::int16_t PiercingEffect = 0x115;

// 0x51c129..0x51c2b2 and 0x51db73: the 0x114 that is not ModelEffects' (the /2..6 are
// Effects::ModelEffects::CreateWaveEffect). The common part (life 20, +0xe4 at 0.1f, +0x64 at −2, +0x68 at 1, the
// angles at 0, the start at the position and z + 50) and:
// - the /0 (0x51c186): sixty joints 0x5ac/0 (0x530860) at the position, each with rand() % 40 + 70 of scale;
// - the /1 (0x51c1ca): life 15, +0x74 at 0x2000, z + 80, the scale at (rand() % 50) · 0.01f + 0.1f, +0xe4 at 0.01f,
//   the angles' x at 90 and z the argument's; two joints 0x6e8/0 of scale rand() % 40 + 70 and then x and
//   y + rand() % 100 − 50 each.
// The 0x6e8/0 and the 0x5ac/0 are created by CL11's joints batch (with their light's rand() and the 0x5ac's three,
// FlareJoints.cpp); with the joints full, it goes on without creating them.
[[nodiscard]] Core::Expected<void, std::string> BeginTheWave(Effects::ModelEffect& effect,
                                                             const Kernel::ModelEffectArguments& arguments,
                                                             const HighEffectContext& context);
} // namespace Client::Effects
