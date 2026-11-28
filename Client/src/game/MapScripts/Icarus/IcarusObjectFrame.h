#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::WorldObjects
{
struct WorldObject;
}
namespace Client::WorldObjects
{
struct GameObjectPass;
} // namespace Client::WorldObjects

// 0x602ed0 with map 10 (0x603eef, the table 0x60466c), after the common rand() of 0x602ef4. Contract: 06 § Los
// objetos de Icarus.
namespace Client::MapScripts::Icarus
{

// - 0 to 5: the first time (+0x58 other than −2), clouds 0x622 with the type's subtype, twenty for 0 to 2
//   and ten for 3 to 5, with the object's position, angles and scale, the light (0.1f; 0.1f; 0.1f) and the
//   object as owner; then +0x58 = −2.
// - The 10: the point of bone 3 of the global pose buffer (0x4ec110 with zero offset) and a 0x5ac/0
//   there, with the object's angles, light 1, scale 1 and no owner.
// - 6 to 9, nothing.
// The particles go to the shared pool (0x53fe70); a full one is not an error. Without a pool or with a creation that
// MuRet does not have it cuts with its address, after what it already did.
[[nodiscard]] Core::Expected<void, std::string> StepIcarusObjectFrame(WorldObjects::WorldObject& object,
                                                                      const WorldObjects::GameObjectPass& pass);
} // namespace Client::MapScripts::Icarus
