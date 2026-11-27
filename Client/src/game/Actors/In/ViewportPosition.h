#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Protocol::Actors
{
struct Position;
} // namespace Client::Protocol::Actors

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

// A character of the viewport changes its cell: the 0xDF. Contract: Client/docs/research/06-render-escenas-y-ui.md,
// "Los otros jugadores en el viewport". All *static*, from the ASM.
namespace Client::Actors::In
{
// 0x4c6b40: the character with the whole key (0x5110d0). With type 0xf1 (the one 0x5141e0 creates for types 160, 180,
// 188, 196, 266, 274 and 337): +0xe4 at 20, the cell's X and Y read with their access, +0xd0 and +0xcc with the
// differences times 400 and sound 0x1b (WorldRuntime::lastSoundId); 0x51fe10(position, 0) tries twenty 0x5c5/2
// particles at Z+80, with two draws per try even with the pool full and five more per creation. Otherwise, the
// protected cell at pkt[5] and pkt[6], +0x39a/+0x39b and +0x399 at 1.
[[nodiscard]] Core::Expected<void, std::string>
ApplyViewportPosition(const Protocol::Actors::Position& position, Kernel::WorldState& world, Crt::CrtRandom& random);
} // namespace Client::Actors::In
