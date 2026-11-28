#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::WorldObjects
{
struct ObjectStepGlobals;
struct WorldObject;
} // namespace Client::WorldObjects

// The cases of 0x604ab0 of the Dungeon (map 1, the table 0x605bc0), of Lost Tower (map 4, the table 0x605c50) and of
// the Arena (map 6), after the common alpha and animation. Contract: 06 § Los objetos del Dungeon y de Lost Tower,
// and § Los objetos de la Arena.
namespace Client::MapScripts::Dungeon
{

// Map 1: the 0x16, the 0x17 and the 0x18 set their type's model+0x88 to 1 and their V to the signed remainder of
// ftol(clock) by 1000 times −0.001f (0x6ba1a4, without negating the ftol); the 0x27, the 0x28 and the 0x33 hide
// (+0x58 = −2); the 0x29 and the 0x2a are class 0 fires of 0x52f360 at (0; −30; 240) and (0; 0; 190); the 0x34 drops
// stone 0xda with rand() % 3 == 0 and always hides. A missing borrowed destination cuts with its address after what
// it already wrote.
[[nodiscard]] Core::Expected<void, std::string> StepDungeonObject(WorldObjects::WorldObject& object,
                                                                  Crt::CrtRandom& random,
                                                                  const WorldObjects::ObjectStepGlobals& globals);

// Map 4: the 3 and the 4 scroll the U like map 0's water (−ftol % 1000 times 0.001f); the 0x12 and the 0x17 take the
// blendMesh to 1, and the 0x13 and the 0x14 to 4 with the scrolling U; the 0x18 hides and with rand() & 63 at 0 drops
// the jet 0x5de; the 0x19 hides, and the 0x26 and the 0x27 go through 0x52f550's kick.
[[nodiscard]] Core::Expected<void, std::string> StepLostTowerObject(WorldObjects::WorldObject& object,
                                                                    Crt::CrtRandom& random,
                                                                    const WorldObjects::ObjectStepGlobals& globals);

// Map 6 (case 6 of 0x604ab0, 0x6054fd, without a table): the 0x15 takes the blendMesh to 3 and scrolls the V like the
// U of map 4's 3 and 4 (−ftol % 1000 times 0.001f); the 0x26 hides. Without rand() or borrowed destinations.
[[nodiscard]] Core::Expected<void, std::string> StepArenaObject(WorldObjects::WorldObject& object,
                                                                const WorldObjects::ObjectStepGlobals& globals);

// 0x52f550: with the hero's action between 0xd and 0x21 and the object's x velocity below 0.1 (or NaN), within 50 of
// the hero (the distance with y stored in float in one of the factors), the velocity (dx · −0.4; dy · −0.4; 0), the
// turn (dy · −4; dx · −4) at +0x28/+0x2c and sound 0x7c. Always after: velocity and turn times 0.6, and added to the
// position and the angles (x with the unrounded product).
[[nodiscard]] Core::Expected<void, std::string> KickObject(WorldObjects::WorldObject& object,
                                                           const WorldObjects::ObjectStepGlobals& globals);
} // namespace Client::MapScripts::Dungeon
