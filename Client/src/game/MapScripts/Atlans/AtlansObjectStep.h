#pragma once

#include "Core/Expected.h"

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

// Atlans' cases of 0x604ab0 (map 7, the table 0x605c94 by type − 0x16), after the common alpha and animation.
// Contract: 06 § Los objetos de Atlans.
namespace Client::MapScripts::Atlans
{

// Map 7, with the world clock T and the sine in the x87:
// - the 0x16: +0x58 = −2 and the phase (+0x84) plus 0.1f, stored in float; if the unrounded sum passes 10,
//   it goes back to 0; with the stored phase above 5, a bubble 0x607/0 at the object with its light (+0xf4)
//   and scale 1;
// - the 0x17: blendMesh 0 and its light at sin(T · 0.002f) · 0.3f + 0.5;
// - the 0x20 and the 0x22: blendMesh 1 and their light at (sin(T · 0.004f) + 1) · 0.5;
// - the 0x26: blendMesh 0;
// - the 0x28: blendMesh 0, the animation speed (+0xd8) at 0.05f and the light at sin(T · 0.004f) · 0.3f + 0.5.
// Without the particle pool, or with a sine outside the PC53 profile, it cuts with its address after what it already
// wrote.
[[nodiscard]] Core::Expected<void, std::string> StepAtlansObject(Kernel::WorldObject& object, Crt::CrtRandom& random,
                                                                 const Kernel::ObjectStepGlobals& globals);
} // namespace Client::MapScripts::Atlans
