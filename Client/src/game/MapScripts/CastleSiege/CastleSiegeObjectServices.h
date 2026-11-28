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

namespace Client::MapScripts::CastleSiege
{

// 0x42c9f0 (0x602ed0, after 0x4059d0 and 0x438c10), with map 30, after the common rand() of 0x602ef4 (which it does
// not use). Without the siege:
// - the 0: with rand() % 3 at 0, the spark 0x6ec/0 over the object, with light (1, 1, 1), its scale and no owner;
// - the 0x29: the blendMesh light (+0x68) at sin(T · 0.002f) · 0.3f + 0.7f, which stays in the object;
// - the 0x34: the fall 0x6f4/6 over the object, with light (1, 1, 1), its scale and no owner, without rand();
// - the 0x36: the splash 0x6f2 + (rand() & 1) with subtype 0 and scale 1.
// With the siege, the 0 does nothing, the 0x2a (with rand() % 3 at 0) releases the smoke 0x5f2/0x15 with light
// (1, 1, 1), its scale and no owner, the 0x35 does what the 0 does without the siege, and the 0x34 and the 0x36 do
// nothing. The other types do nothing. Without the pool, the creation cuts after its rand(), with its address.
[[nodiscard]] Core::Expected<void, std::string> StepCastleSiegeObjectFrame(WorldObjects::WorldObject& object,
                                                                           const WorldObjects::GameObjectPass& pass);
} // namespace Client::MapScripts::CastleSiege
