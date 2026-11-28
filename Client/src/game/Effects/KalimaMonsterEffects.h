#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// The model effects of the Kalima monsters (0x43a7a0) that are high types: the 0x11a (Skill\seamanFX), its creation
// (0x51c884) and step (0x5266ea), and the 0x615 (not drawn: drops the 0x615/5 rays), its creation (0x51ed79) and step
// (0x52b4a7). 06, "Los efectos con modelo de los tipos altos".
namespace Client::Effects
{
struct ModelEffect;
} // namespace Client::Effects

namespace Client::Effects
{
struct HighEffectStepContext;

inline constexpr std::int16_t SeamanEffect = 0x11a;
inline constexpr std::int16_t ThunderEffect = 0x615;

// 0x51c884..0x51c8e3 and 0x51e3ca: +0x64 at −2, +0x68 at 0.2f, scale 0.4f, life 20, +0xe4 at 120, +0xd8 at −40, speed
// (0, −40, 0), light at (1, 1, 1) and the start at the position. Any subtype; no rand().
void BeginTheSeaman(Effects::ModelEffect& effect);

// 0x5266ea..0x5267c2: the scale grows 0.02f, z is the start's plus +0xe4, +0xd8 drops 10 and +0xe4 drops 20 (no less
// than 60); the speed becomes (0, +0xd8, 0). With the local light `luminosity` (0x5208c5) times (0.1f, 0.3f, 0.6f),
// four particles 0x6f4/1 in its place with its angles and the dynamic light of radius 3. No rand() of its own.
[[nodiscard]] Core::Expected<void, std::string> StepTheSeaman(Effects::ModelEffect& effect, float luminosity,
                                                              const HighEffectStepContext& context);

// 0x51ed79..0x51edf4: life 10. With an owner (+0x108, 0x43ab5f's character) and subtype 0, the position shifted
// (−25, −80, 0) rotated by the owner's angles; otherwise, nothing more. No rand().
void BeginTheThunder(Effects::ModelEffect& effect);

// 0x52b4a7..0x52b568: with under 5 life (edi, 0x5208df), even and subtype 0, a point shifted (rand() % 64 − 32) in x
// and y and the joint 0x615/5 there with its angles, no owner and scale rand() % 10 + 50 (0x52b560; two more
// rand() with room in the joints). Otherwise, nothing.
[[nodiscard]] Core::Expected<void, std::string> StepTheThunder(Effects::ModelEffect& effect,
                                                               const HighEffectStepContext& context);
} // namespace Client::Effects
