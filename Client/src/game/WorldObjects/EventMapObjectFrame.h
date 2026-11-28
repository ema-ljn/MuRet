#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::WorldObjects
{
struct WorldObject;
}
// 0x602ed0 with Devil Square (map 9, 0x603dd7) and with Blood Castle (11 to 17, 0x604152), and 0x6044ed's chain with
// Chaos Castle (18 to 23, 0x4059d0), after 0x602ef4's common rand(). Contract: 06 § Los objetos de Devil Square y de
// Blood Castle, and § Los objetos de Chaos Castle.
namespace Client::WorldObjects
{
struct GameObjectPass;

// Type 2: with rand() & 3 at 0, a 0x68/0 at bone 23 of the global pose (0x4ec110 with (−15; 0; 0)); with another
// rand() & 3 at 0, another at bone 31; and always one more at 23. All three with the object's angles, light 1, scale 1
// and no owner. The other types, nothing.
[[nodiscard]] Core::Expected<void, std::string> StepDevilSquareObjectFrame(const WorldObjects::WorldObject& object,
                                                                           const GameObjectPass& pass);

// With the world clock T and the sine in the x87:
// - the 0xb: seven 0x5ac sprites of 0.5 at (0; 0; 2) on bones 1, 2, 4, 6, 9, 10 and 11, unrotated, with light
//   (a; a · 0.5; 0), a = (sin((angle z · 20 + T) · 0.001f) + 1) · 0.5 (y from the unrounded a);
// - the 0xd: one 0x60f sprite at bone 3, with v = sin(T · 0.001f) · 0.3f + 0.7f stored as float, light (v; v; v)
//   and scale v + 0.5;
// - the 0x25: with rand() & 1 at 0, ftol(phase + 2) % 4 and the phase (+0x84) plus 1; with 0, a 0x6ac/0 and a
//   0x6ab/0 at the object. Then, with another rand() & 1 at 0, ftol(phase) % 4 and the phase plus 1; with 0, a
//   0x622/6 and a 0x6ab/1 with light 1 and a 0x60f/4 with light (1; 0.8f; 0.8f) and scale 0.19f. All with the
//   object's angles and no owner.
// The other types, nothing. A sine or ftol outside the accredited profile, a missing pose, sprites or pool, and a
// creation MuRet lacks stop with their address, after what was already done.
[[nodiscard]] Core::Expected<void, std::string> StepBloodCastleObjectFrame(WorldObjects::WorldObject& object,
                                                                           const GameObjectPass& pass);

// 0x4059d0, with 0x6c634c's area (`pass.chaosCastleArea`, an unsigned byte):
// - 0 to 3: with +0x8a nonzero (set by 0x4055b0) and +0x58 other than −2, the point of bone 1 of the global pose;
//   with +0x60 at 10, joint 0x615/2 there (scale rand() % 10 + 60, the object's angles) and sound 0x9c + (rand() &
//   1); +0x60 below 5 turns +0x8a off and, otherwise, drops by one. Then, like the 4;
// - the 4, the 5 and 0xd to 0x11: with the area under 2 or at 0xff, nothing; otherwise, +0x58 = −2;
// - 6 to 0xc: the first time (+0x58 other than −2), 0x622 clouds with the object's position, angles and scale and the
//   object as owner: ten of subtypes 0, 1 and 2 (the 6, the 7 and the 8), five of 3, 4 and 5 (the 9, the 0xa and the
//   0xb), all with light (0.05f; 0.05f; 0.1f), and seven of 7 (the 0xc) with light 0.3f. Then, +0x58 = −2;
// - 0x12 to 0x15: with the area at 7 or 8, +0x58 = −1; otherwise, −2;
// - 0x18 to 0x1d: with the area at 4 or 5, −1; with another under 8, nothing; otherwise, −2;
// - 0x1e to 0x23: with the area at 1 or 2, −1; with another under 5, nothing; otherwise, −2;
// - the 0x16, the 0x17 and the others, nothing.
// Without a pool, a pose, a joint creator or sound, or with a creation MuRet lacks, it stops with its address, after
// what it already did.
[[nodiscard]] Core::Expected<void, std::string> StepChaosCastleObjectFrame(WorldObjects::WorldObject& object,
                                                                           const GameObjectPass& pass);
} // namespace Client::WorldObjects
