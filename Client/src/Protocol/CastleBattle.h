#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/ProtocolError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

// 04 § Castle Siege, la batalla (CL9 hito 13): lo que 0x4e2b10 reparte con el B9 que llega (0x4e58b0, tabla
// 0x4e61f8: 01 → 0x4e0750, 02 → 0x4e0790, 03 → 0x4e07f0, 05 → 0x4e0860; 04 y lo demás al default), el BA (0x4e5929 →
// 0x4cf1b0), el BB (0x4e57f5 → 0x4e0f30) y el B2 20 (0x4e572d → 0x4e04e0), sin aplicar nada. El GameServer 0.97 del
// laboratorio no tiene Castle Siege: cada byte sale del ASM del cliente, *estático*.
//
// Como en el hito 12 (Protocol/CastleSiege.h), el subcódigo del B9 y del B2 es pkt[3] en un C1 y pkt[4] en los demás,
// y después cada handler lee a desplazamientos fijos desde el primer byte, sin mirar el tipo ni el largo. Lo que el
// caso lee y no llegó es TooShort; los bytes sobrantes se ignoran.
namespace Client::Protocol::CastleBattle
{
inline constexpr std::uint8_t BattleOpcode = 0xb9;
inline constexpr std::uint8_t NpcLevelSubcode = 0x01;      // 0x4e0750
inline constexpr std::uint8_t OwnerMarkSubcode = 0x02;     // 0x4e0790
inline constexpr std::uint8_t HuntZoneInfoSubcode = 0x03;  // 0x4e07f0
inline constexpr std::uint8_t HuntZoneEntrySubcode = 0x05; // 0x4e0860
inline constexpr std::array<std::uint8_t, 4> BattleSubcodes{NpcLevelSubcode, OwnerMarkSubcode, HuntZoneInfoSubcode,
                                                            HuntZoneEntrySubcode};
inline constexpr std::uint8_t ActorFieldOpcode = 0xba;
inline constexpr std::uint8_t MinimapMembersOpcode = 0xbb;
// El B2 20, en la tabla 0x4e6140 del B2 del hito 12.
inline constexpr std::uint8_t CastleOpcode = 0xb2;
inline constexpr std::uint8_t SwitchHolderSubcode = 0x20;

// B9 01 (0x4e0750): la clave (pkt[4] << 8 | pkt[5]) y el byte que va a +0x93 de su personaje (pkt[6]). *Inferido*:
// el nivel de una puerta o una estatua, que el alta del 13 también escribe (0x4c9ee9) y que los dibujos del asedio
// comparan con 2, 3 y 5 (0x42e3d0, 0x42dfe0, 0x42a2b0).
struct NpcLevel
{
    std::uint16_t key = 0;
    std::uint8_t level = 0;
};

// B9 02 (0x4e0790): los 32 bytes de pkt[4..0x23], la marca del gremio dueño en 8 × 8 colores de 4 bits.
inline constexpr std::size_t PackedMarkSize = 32;
struct OwnerMark
{
    std::array<std::uint8_t, PackedMarkSize> packed{};
};

// B9 03 (0x4e07f0): pkt[4]; distinto de 0, también pkt[5] y los dwords de pkt+8, pkt+0xc y pkt+0x10 (MOV r32, el
// byte de abajo primero), que van a [0x7d53700].
struct HuntZoneInfo
{
    std::uint8_t mode = 0;    // pkt[4], a +0x16
    std::uint8_t allowed = 0; // pkt[5], a +0x15 con SETNZ
    std::uint32_t fee = 0;    // pkt+8, a +0x18 y +0x1c
    std::uint32_t at0c = 0;   // pkt+0xc, a +0x24
    std::uint32_t at10 = 0;   // pkt+0x10, a +0x20
};

// B9 05 (0x4e0860): pkt[4].
struct HuntZoneEntryResult
{
    std::uint8_t result = 0;
};

// BA (0x4cf1b0): la clave (pkt[3] << 8 | pkt[4], también en un C2), cuál (pkt[5]) y, solo con cuál en 0x28, el byte
// que va a +0x1bc de su personaje (pkt[6]).
inline constexpr std::uint8_t ActorField1bcKind = 0x28;
struct ActorField
{
    std::uint16_t key = 0;
    std::uint8_t kind = 0;
    std::uint8_t value = 0;
};

// BB (0x4e0f30): la cuenta (pkt[4], un byte sin signo) y las ternas (índice, x, y) desde pkt+5, que van a
// 0x411ed0(índice + 1, x, y) del panel del asedio de la clase 2. Solo las lee con ese panel: acá se leen siempre.
struct MinimapMember
{
    std::uint8_t index = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};
struct MinimapMembers
{
    std::vector<MinimapMember> members;
};

// B2 20 (0x4e04e0): los primeros 0x1b bytes del paquete tal cual (REP MOVSD × 6, MOVSW y MOVSB desde pkt+0), que van a
// la mitad del objeto de 0x5767924 que elige la clave (pkt[4] << 8 | pkt[5]): 0x13d la primera y otra la segunda.
inline constexpr std::size_t SwitchHolderSize = 0x1b;
struct SwitchHolder
{
    std::uint16_t key = 0;
    std::array<std::uint8_t, SwitchHolderSize> bytes{};
};

using CastleBattlePacket =
    std::variant<NpcLevel, OwnerMark, HuntZoneInfo, HuntZoneEntryResult, ActorField, MinimapMembers, SwitchHolder>;

// El paquete de la batalla que es, por su opcode y, en el B9 y el B2, su subcódigo. Otro opcode, otro subcódigo del B9
// (el 04 y desde el 06 caen al default, 0x4e58c8) o del B2: WrongPacket.
[[nodiscard]] Core::Expected<CastleBattlePacket, ProtocolError> ReadCastleBattlePacket(const Network::Packet& packet);
} // namespace Client::Protocol::CastleBattle
