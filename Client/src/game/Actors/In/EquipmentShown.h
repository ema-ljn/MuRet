#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Protocol::Actors
{
struct EquipmentShown;
} // namespace Client::Protocol::Actors

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Actors
{
class IViewportHooks;
} // namespace Client::Actors

// Another character changes what it shows: the 0x25. Contract: Client/docs/research/06-render-escenas-y-ui.md, "Los
// otros jugadores en el viewport". All *static*, from the ASM.
namespace Client::Actors::In
{
// 0x4c8380: the character with the whole key (0x5110d0, the 400 if there is none) and the item of pkt[5..]: the type
// (0x54b3a0) in the slot of the high nibble of pkt[6], with 0x1fff for no item. The hands (0, 1) with the level
// (0x513340 of the low nibble: from 8 to 15, 0) and the excellent bits (pkt[8] & 0x3f); emptying the left one releases
// the published hero's raven, not this character's (Q-C803), and filling it arms it again (0x420d60). The five of the
// body (2 to 6) with the set (pkt[9]); an empty one, the class's part. The wings (7) with the level at 0. The pet (8):
// empty, 0x443d40; otherwise, the helper by type (0x1a00, 0x1a02, 0x1a03, 0x1a04 and Fenrir 0x1a25 with +0x31b at the
// excellent bits) with 0x443fa0. From 9 to 15, nothing. At the end, 0x4053f0 and 0x512930. What other modules do,
// through `hooks`.
[[nodiscard]] Core::Expected<void, std::string> ApplyEquipmentShown(const Protocol::Actors::EquipmentShown& shown,
                                                                    Kernel::WorldState& world,
                                                                    const IViewportHooks& hooks);
} // namespace Client::Actors::In
