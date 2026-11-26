#pragma once

#include "Protocol/Outgoing.h"

#include <cstdint>

// The AF 01 and the D0 00 (04, "Las ventanas de los NPC"): what the yes of the 0x9a (0x628594) and of the 0xaa
// (0x62a480) in 0x623e70 send. Both build the C1 one byte at a time with the XOR32 of each new byte (the 32-byte key
// from 0xe7 0x6d 0x3a, from pkt[3]) and send it with send() in the clear, without the protection table or rand(). The
// GameServer 0.97 does not handle them: ProtocolCore (0x414180) has no case for them and its default logs and closes
// the connection (CloseClient). The ASM is the oracle.
namespace Client::Protocol::Npc
{
// 0x628594..0x628947: C1 06 AF 01 with the dialogs' item level ((0x78a59cc >> 3) & 0xf, 0x628702..0x628710) and the
// low byte of 0x78a6758 (the place of the cursor's item, 0x6287f7).
[[nodiscard]] Outgoing WriteChaosCastleEntry(std::uint8_t level, std::uint8_t slot);

// 0x62a480..0x62a5e8: C1 04 D0 00.
[[nodiscard]] Outgoing WriteNoticeYes();
} // namespace Client::Protocol::Npc
