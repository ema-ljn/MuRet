#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Protocol::Actors
{
struct Players;
} // namespace Client::Protocol::Actors

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

namespace Client::Actors
{
class IViewportHooks;
} // namespace Client::Actors

// The other players enter the viewport: the 0x12. Contract: Client/docs/research/06-render-escenas-y-ui.md, "Los otros
// jugadores en el viewport". All *static*, from the ASM. What MuRet does not have stops with an error, with its
// address, after what the original does before it. The searches 0x5132d0 (the live character with a name), 0x5178f0
// (an int in an array) and 0x517920 (the players near a point) have no callers or pointers in the binary: they are not
// ported.
namespace Client::Actors::In
{
// The dispatcher (0x4e372e..0x4e37b8) stores the whole packet in the queue (the session, with CopyToPacketRing, before
// reading it) and 0x4c8860 walks the records. Each one: if the name (the ten bytes, up to its 0) contains "webzen"
// (0x576960), nothing. Otherwise, with state 1 it keeps from a live character with the key (0x5110d0) the low byte of
// +0x1f2, +0x368, +0x369, +0x370, +0x360, +0x78 and +0x372; the creation (CreateActor, type 0x20a, angle 0), which
// restores them from what it kept (+0x1f2 without the high byte, Q-C800); the class from +4, the skin at 0, +0x88 at 1
// and the PK; by state, 1: 0x53d930 and the packet to the queue again (Q-C801), 2: action 0xc5 (0xc7 for the elf), 3:
// 0xcb (0xcc), 4: 0xc9 (0xca), 0x4ea030; the flags (0x4e6380); the protected cell and +0x39a/+0x39b; the angle,
// (direction − 1) · 45. Just arrived (bit 15): the position at the center of the cell, effect 0x61f (0x4c905f, with the
// actor as owner at +0x108 and key −1) and the alpha at 0. Otherwise, the path to +0x26/+0x27 (0x4eac00) and, if there
// is one, +0x378 at 1. In Kalima (24 to 29 and 36), joint 0x610/8 with the player as owner (0x4c91dc, the class of the
// Atlans fish with JointOwnerKind::Actor; without rand(), and with the joints full nothing). Then the equipment
// (ApplyCharacterSet with the slot from 0x5110d0), the name and +0x1e3 at 0, with state 1 +0x360 and +0x78 again,
// Castle Siege (0x42ab40) and the panel (0x412140). Each cell written or read draws 4 rand() on closing; the creation,
// the 28 of 0x511290 and those of its helpers. What other modules do, through `hooks`.
[[nodiscard]] Core::Expected<void, std::string> ApplyViewportPlayers(const Protocol::Actors::Players& players,
                                                                     const Scenes::WorldEntryResources& resources,
                                                                     const IViewportHooks& hooks);
} // namespace Client::Actors::In
