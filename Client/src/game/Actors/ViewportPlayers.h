#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Protocol::Actors
{
struct Players;
struct Leave;
struct Transformed;
struct TransformedRecord;
struct EquipmentShown;
struct Position;
} // namespace Client::Protocol::Actors

// The other players enter and leave the viewport: the 0x12, the 0x14, the 0x45, the 0x25 and the 0xDF. Contract:
// Client/docs/research/06-render-escenas-y-ui.md, "Los otros jugadores en el viewport". All *static*, from the ASM.
// What MuRet does not have stops with an error, with its address, after what the original does before it. The searches
// 0x5132d0 (the live character with a name), 0x5178f0 (an int in an array) and 0x517920 (the players near a point) have
// no callers or pointers in the binary: they are not ported.
namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Kernel
{
class WorldState;
struct ActorState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

namespace Client::Actors
{

// Shared by the viewport's creations (the players, the 45, the DF and the monsters of MonsterViewport.h): a write of
// the protected cell (+0x41c or +0x420) with its access open: 4 rand() on closing if no access is left open.
void WriteCell(std::uint8_t value, std::uint8_t& cell, std::uint8_t& depth, Crt::CrtRandom& random);
// A read, with its access (0x40b800 or the inline close).
[[nodiscard]] std::uint8_t ReadCell(const std::uint8_t& cell, std::uint8_t& depth, Crt::CrtRandom& random);
// The X and then the Y of the protected cell, and +0x39a/+0x39b with the same values.
void WriteTheCells(Kernel::ActorState& actor, std::uint8_t x, std::uint8_t y, Crt::CrtRandom& random);
// (high nibble − 1) · 45 (0x6b8848, with the 1 from 0x6b87d4).
[[nodiscard]] float AngleOf(std::uint8_t directionAndPk);
// The X and then the Y, read from the table, to the center of the cell.
void PutInTheCellCenter(Kernel::ActorState& actor, Crt::CrtRandom& random);
// The Y, the X and 0x4eac00(X, Y, target, +0x3e8, 0); +0x378 at 1 only if there is a path.
[[nodiscard]] Core::Expected<void, std::string> WalkToTheTarget(Kernel::ActorState& actor, std::uint8_t targetX,
                                                                std::uint8_t targetY,
                                                                const Scenes::WorldEntryResources& resources);

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
// the 28 of 0x511290 and those of its helpers.
[[nodiscard]] Core::Expected<void, std::string> ApplyViewportPlayers(const Protocol::Actors::Players& players,
                                                                     const Scenes::WorldEntryResources& resources);

// 0x4ca780: for each key, with the siege information (0x5767924), the full key 0x13d sets its bytes 4..6 to 0 and any
// other key bytes 0x1f..0x21 (Q-C802); 0x510ff0 (RemoveActor) with the key without bit 15; and 0x451630 looks up the
// personal shop sign of that key in the map at 0x7245c8 (the first in its order) and 0x451430 removes it.
void ApplyViewportLeave(const Protocol::Actors::Leave& leave, Kernel::WorldState& world);

// 0x4c92a0: with "webzen" in the name, the record does nothing. Otherwise, the published hero's class (0x4c9328), the
// creation of the transformed character or of the monster of type +4 (CreateMonster, 0x5141e0, without looking at
// whether it returns 0) and FinishTheTransformed.
[[nodiscard]] Core::Expected<void, std::string> ApplyTransformed(const Protocol::Actors::Transformed& transformed,
                                                                 const Scenes::WorldEntryResources& resources);

// 0x4c9356..0x4c9762, what 0x4c92a0 does with the character `index` that 0x5141e0 returned: with +0x376 at 7 the scale
// at 0.8; the published hero's class it read before the creation (`heroClass`, 0x4c9328), the PK, +0x88 and +0x3e3 at
// 1; the flags (0x4e6380 and 0x42ab40); the protected cell, +0x39a/+0x39b and the angle. Just arrived, the position,
// the alpha at 0, circle 0xec (0x4c965c) and ray 0x5d5/2 that follows it (0x4c9674), both with it as owner; otherwise,
// the path (0x4eac00) with the Y read before the X. Then the name, +0x1e3 at 0 and 0x512a30 on the published hero.
[[nodiscard]] Core::Expected<void, std::string> FinishTheTransformed(std::size_t index,
                                                                     const Protocol::Actors::TransformedRecord& record,
                                                                     std::uint8_t heroClass,
                                                                     const Scenes::WorldEntryResources& resources);

// 0x4c8380: the character with the whole key (0x5110d0, the 400 if there is none) and the item of pkt[5..]: the type
// (0x54b3a0) in the slot of the high nibble of pkt[6], with 0x1fff for no item. The hands (0, 1) with the level
// (0x513340 of the low nibble: from 8 to 15, 0) and the excellent bits (pkt[8] & 0x3f); emptying the left one releases
// the published hero's raven, not this character's (Q-C803), and filling it arms it again (0x420d60). The five of the
// body (2 to 6) with the set (pkt[9]); an empty one, the class's part. The wings (7) with the level at 0. The pet (8):
// empty, 0x443d40; otherwise, the helper by type (0x1a00, 0x1a02, 0x1a03, 0x1a04 and Fenrir 0x1a25 with +0x31b at the
// excellent bits) with 0x443fa0. From 9 to 15, nothing. At the end, 0x4053f0 and 0x512930.
[[nodiscard]] Core::Expected<void, std::string> ApplyEquipmentShown(const Protocol::Actors::EquipmentShown& shown,
                                                                    const Scenes::WorldEntryResources& resources);

// 0x4c6b40: the character with the whole key (0x5110d0). With type 0xf1 (the one 0x5141e0 creates for types 160, 180,
// 188, 196, 266, 274 and 337): +0xe4 at 20, the cell's X and Y read with their access, +0xd0 and +0xcc with the
// differences times 400 and sound 0x1b (WorldRuntime::lastSoundId); 0x51fe10(position, 0) tries twenty 0x5c5/2
// particles at Z+80, with two draws per try even with the pool full and five more per creation. Otherwise, the
// protected cell at pkt[5] and pkt[6], +0x39a/+0x39b and +0x399 at 1.
[[nodiscard]] Core::Expected<void, std::string> ApplyViewportPosition(const Protocol::Actors::Position& position,
                                                                      const Scenes::WorldEntryResources& resources);
} // namespace Client::Actors
