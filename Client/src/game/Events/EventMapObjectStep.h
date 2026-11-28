#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <string>

namespace Client::Kernel
{
struct ObjectStepGlobals;
struct WorldObject;
} // namespace Client::Kernel

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Particles
{
class Particles;
} // namespace Client::Particles

// What 0x604ab0 does with the objects of Devil Square (map 9) and the Blood Castles (maps 11 to 17), beyond the common
// part. Contract: 06 § Los objetos de Devil Square y de Blood Castle.
namespace Client::Events
{

// 0x604ac0..0x604ba0, at the start of 0x604ab0 with map 9, before the roofs and the alpha, with each object that
// arrives: if ftol(clock) % 4000 (signed) is below 1000 and rand() % 100 gives 0, a dynamic light of radius 12 with
// L = (rand() % 12 + 4) · 0.1f and color (L · 0.2f, L · 0.3f, L · 0.5), at (hero x + rand() % 1200 − 600, hero y +
// rand() % 1200 − 600), y before x. Then, always, sound 1 looped (0x41ed10(1, 0, 1)). A clock without a vetted ftol,
// or a missing hero, light or sound, stops with its address after what was already done.
[[nodiscard]] Core::Expected<void, std::string> StepDevilSquarePrelude(Crt::CrtRandom& random,
                                                                       const Kernel::ObjectStepGlobals& globals);

// 0x6062e6..0x6063c3, the maps 11 to 17 part of 0x6061c0, once per step and before checking for seen objects: a point
// at (hero x + rand() % 900 − 300, hero y + rand() % 900 − 300, hero z + rand() % 50 + 250), each sum in the x87 and
// stored as a float, and with rand() & 3 at 0 the orbit 0x60f/3 there, with angles at 0, light (1, 1, 1) and scale
// 0.19f. Without the particle pool, or if the orbit fails, stops at 0x6063be after the rand().
[[nodiscard]] Core::Expected<void, std::string> StepBloodCastlePrelude(Crt::CrtRandom& random, const glm::vec3& hero,
                                                                       Particles::Particles* particles,
                                                                       const float& worldClock);

// 0x605991, in maps 11 to 17 after the alpha and the animation: 9 and 10 stay with +0x58 at −2, except with +0x8a at 4
// (the gate 0x5ff960 opened). Other types, nothing.
void StepBloodCastleObject(Kernel::WorldObject& object);
} // namespace Client::Events
