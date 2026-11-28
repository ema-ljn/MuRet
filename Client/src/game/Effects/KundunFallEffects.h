#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

// Kundun's fall (0x43eeb9..0x43f144, from its body in the dying action, MonsterBodyChain): the eight pieces
// 0x11d..0x124 (Monster\cd71a..cd71h, which only Kalima loads) it drops at bone 4 and the 0x128/2, which re-enters
// 0x518cf0 as the 0x127 (0x518f51) with itself as owner. The creations (0x51e234 and 0x51cf6d) and steps (0x52a17b and
// 0x5277ae). Their draw is the common path (0x52da92 → 0x602e90): 0x6002e0 has no case of its own for them. 06, "Los
// efectos con modelo de los tipos altos".
namespace Client::Kernel
{
struct ModelEffectArguments;
} // namespace Client::Kernel

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Effects
{
struct ModelEffect;
class ModelEffects;
} // namespace Client::Effects

namespace Client::Effects
{
struct HighEffectStepContext;

inline constexpr std::int16_t FirstKundunPieceEffect = 0x11d;
inline constexpr std::int16_t LastKundunPieceEffect = 0x124;
inline constexpr std::int16_t KundunEchoEffect = 0x127;

// 0x5f93c0's terrain height at (x, y), what it leaves on the x87 stack.
using KundunTerrainHeight = std::function<double(float, float)>;

// 0x51e234..0x51e348, a piece after the prefix, by its subtype (0x51fbe8's table):
// - /1: life 100 and scale 2.
// - /2 and /3: the start (+0x17c) at the x and y, its z at the terrain height minus the key (+0x8a, MOVSX) minus 80
//   (one fstp); the speed at 0, life 500, scale 2 and the copied angles' x (+0x28) at rand() % 2, which the /2 sets
//   back to 0.
// - /4: the start at the x and y and the terrain height minus the key; the speed at 0, the action (+0x111) at 0,
//   life 170 and scale 2.
// - /5: the action at 0, life 170 and scale 2.
// All, the key at 0xffff at the end. Without the terrain height, the /2, the /3 and the /4 cut (0x51e26e, 0x51e2ee).
[[nodiscard]] Core::Expected<void, std::string>
BeginTheKundunPiece(Effects::ModelEffect& effect, Crt::CrtRandom& random, const KundunTerrainHeight& terrainHeight);

// 0x518cf0(piece, ...) on `effects`, as Kundun's fall asks for it: the prefix (Effects::ModelEffects::Begin) and
// BeginTheKundunPiece. With the table full, nothing and no rand(). Returns the slot.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateTheKundunPiece(Effects::ModelEffects& effects, const Kernel::ModelEffectArguments& arguments,
                     Crt::CrtRandom& random, const KundunTerrainHeight& terrainHeight);

// 0x51cf6d..0x51cf91 and 0x51d9ff, the 0x127 of any subtype: +0xd8 at 0.08f, life 200, +0x64 at −2, scale 1.8f and
// light at 0.5. No rand().
void BeginTheKundunEcho(Effects::ModelEffect& effect);

// 0x518f51..0x518f90: the 0x128/2 of slot `call` re-enters 0x518cf0 as the 0x127/0 with its position, angles and light,
// itself as owner (+0x108), the key at −1, scale 0 (the prefix's 0.9f) and BeginTheKundunEcho. With the table full,
// nothing.
std::optional<std::size_t> CreateTheKundunEcho(Effects::ModelEffects& effects, std::size_t call);

// 0x52a17b..0x52a683, a piece's step by its subtype (0x52d0fc's table):
// - /1: the alpha 0.01f less.
// - /4: with 140 life or more, the frame (+0x114) at 0; otherwise, the alpha 0.01f less and, above the start, the
//   speed's z 3.5 less, or the speed at 0.
// - /5: with 150 life or more, the frame at 0; otherwise, the alpha 0.01f less.
// - /2 and /3: while life does not drop below (0x5e − +0x89) · 5, the /3 with over 385 life shakes: with rand() % 2 at
//   0, some local angles (0, 0, rand() % 360) and, with another rand() % 2 at 0, the joint 0x6ee/8 with no owner with
//   them (width 50, key 0); then, with rand() % 3 at 0, the particle 0x5f5/2 with the effect's angles (light 0.5,
//   scale ((rand() & 31) + 48) · 0.02f). Then it falls:
//   above 350, with the speed at (0, 0, 0), the x and y at rand() % 2 · 2 − 2, and z 6 less; otherwise, with the
//   speed's z other than −3, the Kalima wave (0x437860(x · 0.01, y · 0.01, 2, −1000)) and a hundred particles
//   0x6f4/2 (0x52a37d, 0x543d68's creation) in a circle of radius rand() % 30 + 30, 30 lower and with the angles' z
//   rand() % 180 more; always, the particle 0x607/4 at ±20 in x and y and 50 lower, the speed at (0, 0, −3), the
//   rocking of the angles' x between −30 and 30 by rand() % 3 + 1 and, with under 100 life, the alpha 0.1f less.
[[nodiscard]] Core::Expected<void, std::string> StepTheKundunPiece(Effects::ModelEffect& effect,
                                                                   const HighEffectStepContext& context);

// 0x5277ae..0x527ad1, the 0x127's step:
// - With an owner (+0x108, the 0x128/2): with its key (+0x8a) at 0, the frame at 0; otherwise, the owner at 0.
// - Without an owner and the frame past 6: the key one more and z key · 0.8f more; at the position with
//   rand() % 120 − 60 in x and y and rand() % 60 in z, three particles 0x5f2/0x14 in white of scale 10.
// - Without an owner and the frame up to 6: +0x89 one more; the angles' z toward 45 by +0x89 · 0.3f; the x and y
//   toward the hero ([0x743a170]) plus 300 in x and minus 300 in y by +0x89 · 0.2f; the 0xc2/0 (0x527a97, key 0) at
//   ±600 in x and y with angles (0, 0, (rand() % 10) · 20) and the light in white; scale 0.02f more and the camera
//   shake (0x7d325c8) at (rand() % 8 − 8) · 0.1f.
[[nodiscard]] Core::Expected<void, std::string> StepTheKundunEcho(Effects::ModelEffect& effect,
                                                                  const HighEffectStepContext& context);
} // namespace Client::Effects
