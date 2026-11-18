#pragma once

#include "Protocol/Outgoing.h"

#include <cstdint>

// 04 § Las ventanas de los NPC, el AF 01 y el D0 00 (CL9 hito 6): lo que mandan los sí del 0x9a (0x628594) y del 0xaa
// (0x62a480) en 0x623e70. Los dos arman el C1 de a un byte con el XOR32 de cada byte nuevo (la clave de 32 bytes desde
// 0xe7 0x6d 0x3a, desde pkt[3]) y lo mandan con send() en claro, sin la tabla de protección ni rand(). El GameServer
// 0.97 no los atiende: ProtocolCore (0x414180) no tiene su caso y su default loguea y cierra la conexión
// (CloseClient). El ASM es el oráculo.
namespace Client::Protocol::DialogAnswers
{
inline constexpr std::uint8_t ChaosCastleOpcode = 0xaf;
inline constexpr std::uint8_t ChaosCastleEnterSubcode = 0x01;
inline constexpr std::uint8_t NoticeAnswerOpcode = 0xd0;
inline constexpr std::uint8_t NoticeYesSubcode = 0x00;

// 0x628594..0x628947: C1 06 AF 01 con el nivel del ítem de los diálogos ((0x78a59cc >> 3) & 0xf, 0x628702..0x628710)
// y el byte bajo de 0x78a6758 (el lugar del ítem del cursor, 0x6287f7).
[[nodiscard]] Outgoing WriteChaosCastleEntry(std::uint8_t level, std::uint8_t slot);

// 0x62a480..0x62a5e8: C1 04 D0 00.
[[nodiscard]] Outgoing WriteNoticeYes();
} // namespace Client::Protocol::DialogAnswers
