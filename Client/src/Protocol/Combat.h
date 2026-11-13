#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <cstdint>

// 04 § Pegar, el daño y la muerte (CL9 hito 3): el D7 que manda el cliente al pegar (0x5650c0, que FoxAnti
// convierte en A4) y lo que llega cuando alguien muere: el 0x16 y el 0x9C con la experiencia (0x4cf200, 0x4cfce0) y el
// 0x17 (0x4d0a30). El D7 que llega (0x4caae0) es de Protocol::Movement.
namespace Client::Protocol::Combat
{
inline constexpr std::uint8_t AttackOpcode = 0xd7;
// 0x565a7b..0x565ae5: la acción del D7, siempre 0x78 (el 0x18 que la devuelve la lleva al otro, 0x4f4a10).
inline constexpr std::uint8_t AttackAction = 0x78;
inline constexpr std::uint8_t KillExperienceOpcode = 0x16;
inline constexpr std::uint8_t DeathOpcode = 0x17;
inline constexpr std::uint8_t ExtendedKillExperienceOpcode = 0x9c;

// 0x5650c0 (0x565a49..0x56601e): C1 07 D7 claveH claveL acción rumbo, XOR32 sobre los bytes 3 a 6 y por el socket
// directo, sin rand(). La clave es +0x1f4 del blanco, entera: el bit 15 viaja si el blanco lo tiene.
[[nodiscard]] Outgoing WriteAttack(std::uint16_t key, std::uint8_t action, std::uint8_t direction);

// 0x4cf200 y 0x4cfce0 con el flag de descifrado en 0 (0x4cf2aa, 0x4cfd12): C1 06 F1 03 00 <opcode>, en C3 (el relleno y
// el serial con rand(), como el F1 03 del 1C y del 24).
[[nodiscard]] Outgoing WriteClearKillReport(std::uint8_t opcode);

// La experiencia de una muerte: 0x16 (PMSG_KILLPLAYER_EXP, C3 de 9 bytes, GCKillPlayerExpSend 0x425a70) o 0x9C
// (PMSG_KILLPLAYER_EXT, C3 de 12, la recompensa de Blood Castle 0x49af90).
struct KillExperience
{
    std::uint16_t key;    // (pkt[3] << 8 | pkt[4]) & 0x7fff
    bool shock;           // el bit 15 de la clave: 0x4f5660 sobre el muerto
    std::uint32_t amount; // 0x16: pkt[5] << 8 | pkt[6]; 0x9C: u16le(pkt+6) · 65536 + u16le(pkt+8)
    std::uint16_t damage; // 0x16: pkt[7] << 8 | pkt[8]; 0x9C: pkt[10] << 8 | pkt[11]
};

// 0x4cf200: NumberH NumberL ExpH ExpL DamageH DamageL, en big endian de a bytes.
[[nodiscard]] Core::Expected<KillExperience, ProtocolError> ReadKillExperience(const Network::Packet& packet);
// 0x4cfce0: NumberH NumberL, el relleno de +5 (no lo lee), ExpH y ExpL como WORD little endian y DamageH DamageL.
[[nodiscard]] Core::Expected<KillExperience, ProtocolError> ReadExtendedKillExperience(const Network::Packet& packet);

// 0x17: la muerte de un personaje. 0x4d0a30 lee pkt[3..4] (la clave, sin máscara), pkt[5] (+0x391, la skill que lo
// mató: 0x28 y 0x3b lo empujan) y, con esas dos, pkt[6..7] (quien lo mató). El GameServer 0.97 manda 5 bytes
// (PMSG_DIEPLAYER, GCDiePlayerSend 0x425bb0): el original lee lo que quedó en su buffer de recepción después; MuRet
// lee 0 (Q-C1161).
struct Death
{
    std::uint16_t key;
    std::uint8_t skill;
    std::uint16_t killer;
};
[[nodiscard]] Core::Expected<Death, ProtocolError> ReadDeath(const Network::Packet& packet);
} // namespace Client::Protocol::Combat
