#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <cstdint>

// Hitting, the damage and the death (04, "Pegar, el daño y la muerte"): the D7 the client sends on hitting (0x5650c0,
// which FoxAnti turns into an A4), and what arrives when someone dies: the 0x16 and the 0x9C with the experience
// (0x4cf200, 0x4cfce0) and the 0x17 (0x4d0a30). The D7 that arrives (0x4caae0) is Protocol::Movement's.
namespace Client::Protocol::Combat
{
// 0x565a7b..0x565ae5: the D7's action, always 0x78 (the 0x18 that returns it carries it to the other, 0x4f4a10).
inline constexpr std::uint8_t AttackAction = 0x78;

// 0x5650c0 (0x565a49..0x56601e): C1 07 D7 keyH keyL action heading, the XOR32 over bytes 3 to 6 and by the socket
// directly, with no rand(). The key is the target's whole +0x1f4: bit 15 travels if the target has it.
[[nodiscard]] Outgoing WriteAttack(std::uint16_t key, std::uint8_t action, std::uint8_t direction);

// 0x59cf88..0x59d3d7, from MoveHero's attack click: an elf walking with a ranged weapon first sends its logical cell,
// C1 05 DF X Y, the XOR32 over X and Y and by the socket directly, with no rand(). The rebuilt GameServer has no case
// for the DF (its PMSG_POSISTION_SET is the C1 11, CGPositionRecv 0x4250d0) and FoxAnti does not change it (Q-C1163).
inline constexpr Network::Opcode ElfCellOpcode = PositionOpcode;
[[nodiscard]] Outgoing WriteElfCell(std::uint8_t x, std::uint8_t y);

// 0x4cf200 and 0x4cfce0 with the decryption flag at 0 (0x4cf2aa, 0x4cfd12): C1 06 F1 03 00 <opcode>, as a C3 (the
// padding and the serial with rand(), like the 1C's and the 24's F1 03).
[[nodiscard]] Outgoing WriteClearKillReport(Network::Opcode opcode);

// A death's experience: 0x16 (PMSG_KILLPLAYER_EXP, a 9-byte C3, GCKillPlayerExpSend 0x425a70) or 0x9C
// (PMSG_KILLPLAYER_EXT, a 12-byte C3, Blood Castle's reward 0x49af90).
struct KillExperience
{
    std::uint16_t key;    // (pkt[3] << 8 | pkt[4]) & 0x7fff
    bool shock;           // the key's bit 15: 0x4f5660 on the dead
    std::uint32_t amount; // 0x16: pkt[5] << 8 | pkt[6]; 0x9C: u16le(pkt+6) · 65536 + u16le(pkt+8)
    std::uint16_t damage; // 0x16: pkt[7] << 8 | pkt[8]; 0x9C: pkt[10] << 8 | pkt[11]
};

// 0x4cf200: NumberH NumberL ExpH ExpL DamageH DamageL, big endian byte by byte.
[[nodiscard]] Core::Expected<KillExperience, Network::ProtocolError> ReadKillExperience(const Network::Packet& packet);
// 0x4cfce0: NumberH NumberL, the padding at +5 (not read), ExpH and ExpL as little-endian WORDs, and DamageH DamageL.
[[nodiscard]] Core::Expected<KillExperience, Network::ProtocolError>
ReadExtendedKillExperience(const Network::Packet& packet);

// 0x17, a character's death. 0x4d0a30 reads pkt[3..4] (the key, unmasked), pkt[5] (+0x391, the skill that killed it:
// 0x28 and 0x3b push it) and pkt[6..7] (the killer). The GameServer 0.97 sends 5 bytes (PMSG_DIEPLAYER,
// GCDiePlayerSend 0x425bb0): the original reads what is left in its receive buffer after them; MuRet reads 0
// (Q-C1161).
struct Death
{
    std::uint16_t key;
    std::uint8_t skill;
    std::uint16_t killer;
};
[[nodiscard]] Core::Expected<Death, Network::ProtocolError> ReadDeath(const Network::Packet& packet);
} // namespace Client::Protocol::Combat
