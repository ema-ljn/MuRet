#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Protocol::Actors
{
struct Monsters;
struct Summons;
struct Objects;
} // namespace Client::Protocol::Actors

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

// The monsters and NPCs entering the viewport: the 0x13, the 0x1F and the 0x68 over CreateMonster (0x5141e0). Contract:
// Client/docs/research/06-render-escenas-y-ui.md, "Los monstruos y los NPC en el viewport". *Static*. What MuRet lacks
// cuts with an error and its address, after what the original does before it.
namespace Client::Monsters
{

// 0x4c9790, the newcomer by +0x376: 0x15 and 0x2c with action 1 (0x4ea030, which needs the model's skeleton), +0x112 at
// 1, alpha at 1 and sound 0x13 or 0x14e; 0x35 and 0x36, +0x3e5 at 0x3c (0x4c982b, the brake with effects and rand() of
// 0x505de0), action 1, +0x112 at 1 and alpha at 1, no sound; 0x55, 0x5b, 0x61, 0x72, 0x78 and 0x7e request sound
// 0x173; 0x154, 0x155, 0x158 and 0x159 cut at 0x4c9883 (they read [0x6e5904], P-C60 g, and drop CL11 joints). The rest:
// with model 0x20a, action 0xc1, +0x112 at 0xc1 and alpha at 1; otherwise alpha at 0. The sounds go to
// WorldRuntime::lastSoundId (CL10).
[[nodiscard]] Core::Expected<void, std::string> ArriveAsNew(Kernel::ActorState& actor,
                                                            const Scenes::WorldEntryResources& resources);

// 0x4c9c60, each record: CreateMonster(type, X, Y, key) (an error cuts the loop), the states (0x4e6380), the angle
// (direction − 1) · 45 (with model 0x1b5, by 0 with its cell's Y at 0x5a, by −135 with X at 0x3e and by 90 with X at
// 0xb7, read without opening: what the close left, ActorState::cellXLeft; with the closes at 0 it never hits, Q-C849),
// the protected cell, +0x39a/+0x39b, +0x42d with bit 7 of the high type and +0x93 with its bits 4..6. In Castle Siege
// (30) the +0x376 0x115 cuts at 0x4c9f0f (0x4ea030 and 0x42a110, CL9.5) if +0x78 has 0x20000 or is 0. A newcomer goes
// to ArriveAsNew; otherwise the walk (WalkToTheTarget).
[[nodiscard]] Core::Expected<void, std::string> ApplyViewportMonsters(const Protocol::Actors::Monsters& monsters,
                                                                      const Scenes::WorldEntryResources& resources);

// 0x4ca0b0, each record: types 0x98..0x9e through 0x517400 (CreateMonster, level at type − 0x97, the name by wsprintfA
// with the owner's, +4 at 1 with level 7; a newcomer cuts at 0x51747c, the rand() and the CL11 0x615 joints) and the
// rest through CreateMonster; the states, the angle, the protected cell, +0x39a/+0x39b, +0x88 at 1, the PK (direction
// & 0xf) and, with 6 or more, level at 1. Outside 0x98..0x9e, the name is the owner's (ten bytes, with +0x1e3 at 0),
// Text 485 and the monster's (strcat). A newcomer: ArriveAsNew, circle 0xec (0x518cf0) and particle 0x5d5/2 (0x53fe70)
// owned by the character and, for the summon, frame at 0 and sound 0x61; otherwise the walk.
[[nodiscard]] Core::Expected<void, std::string> ApplySummons(const Protocol::Actors::Summons& summons,
                                                             const Scenes::WorldEntryResources& resources);

// 0x4e0980, each record by its +0: with 1, a player's creation (CreateActor, 0x20a, angle 0) with the class from +1,
// skin and PK at 0, +0x88 at 0x80, the states, the protected cell, +0x39a/+0x39b, +0x428 at 0x96, angle rand() % 360,
// the equipment (ApplyCharacterSet) and the name "   " with +0x1e3 at 0; with 2 or 3, CreateMonster with the type from
// +1 and +2, the states, the cell, +0x39a/+0x39b, +0x428 and the angle, and the Castle Siege 0x115 as in the 13; with
// any other, nothing.
[[nodiscard]] Core::Expected<void, std::string> ApplyViewportObjects(const Protocol::Actors::Objects& objects,
                                                                     const Scenes::WorldEntryResources& resources);
} // namespace Client::Monsters
