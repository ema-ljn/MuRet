#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

// 04 § Castle Siege, las ventanas del castillo (CL9 hito 12): lo que 0x4e2b10 reparte con B2 (la tabla 0x4e6140 y su
// segundo nivel, 0x4df080, 0x4df1a0, 0x4df7d0, 0x4df950 y los casos en línea), B3 (0x4dfc60), B4 (0x4dfce0), B5
// (0x4dfd80), B6 (0x4e0ed0) y B8 (0x4e0700), sin aplicar nada; y lo que mandan las ventanas del castillo. El GameServer
// 0.97 del laboratorio no tiene Castle Siege: cada byte sale del ASM del cliente, *estático*.
//
// El subcódigo del B2 y del B8 es pkt[3] en un C1 y pkt[4] en los demás (0x4e509c, 0x4e5883), el byte que sigue al
// opcode, como lo lee el Dispatcher. Después, cada handler lee sus campos a desplazamientos fijos desde el primer byte
// del paquete, sin mirar el tipo ni el largo: acá también (en un C2 el resultado y el subcódigo son el mismo pkt[4]).
// Un paquete que no trae lo que se lee es TooShort, y una lista que dice más entradas que las que trae, también; los
// bytes sobrantes se ignoran. Los B2 de la batalla (0x11..0x19, 0x1D, 0x1E y 0x20) son del hito 13: no se leen.
namespace Client::Protocol::CastleSiege
{
inline constexpr std::uint8_t CastleOpcode = 0xb2;
inline constexpr std::uint8_t NpcListOpcode = 0xb3;
inline constexpr std::uint8_t SiegeGuildListOpcode = 0xb4;
inline constexpr std::uint8_t RegisteredGuildListOpcode = 0xb5;
inline constexpr std::uint8_t MinimapOpcode = 0xb6;
inline constexpr std::uint8_t KillCountOpcode = 0xb8;
// 0x4e5894: el dispatcher solo llama a 0x4e0700 con el subcódigo en 1.
inline constexpr std::uint8_t KillCountSubcode = 0x01;
// El B9 que manda el guardia de la zona de caza (0x648a30). El B9 que llega es de la batalla (hito 13).
inline constexpr std::uint8_t HuntZoneOpcode = 0xb9;
inline constexpr std::uint8_t HuntZoneEntrySubcode = 0x05;

// Los subcódigos del B2 que lee el hito 12, los mismos en los dos sentidos.
inline constexpr std::uint8_t StateSubcode = 0x00;        // 0x4e50bd
inline constexpr std::uint8_t RegisterSubcode = 0x01;     // 0x4df080
inline constexpr std::uint8_t GiveUpSubcode = 0x02;       // 0x4df1a0
inline constexpr std::uint8_t RegistrationSubcode = 0x03; // 0x4e5186
inline constexpr std::uint8_t RegisterMarkSubcode = 0x04; // 0x4e51fa
inline constexpr std::uint8_t NpcBuySubcode = 0x05;       // 0x4df7d0
inline constexpr std::uint8_t NpcRepairSubcode = 0x06;    // 0x4e52b7
inline constexpr std::uint8_t NpcUpgradeSubcode = 0x07;   // 0x4df950
inline constexpr std::uint8_t TaxInfoSubcode = 0x08;      // 0x4e537b
inline constexpr std::uint8_t TaxRateSubcode = 0x09;      // 0x4e53e7
inline constexpr std::uint8_t MoneyOutSubcode = 0x10;     // 0x4e548a
inline constexpr std::uint8_t SiegeFlagsSubcode = 0x1a;   // 0x4e54f6
inline constexpr std::uint8_t HuntZoneGateSubcode = 0x1f; // 0x4e5532
// Los que registra la sesión. Los demás casos de 0x4e6140 (0x11..0x19, 0x1D, 0x1E y 0x20) son de la batalla (hito
// 13) y 0x0A..0x0F, 0x1B y 0x1C caen al default (0x4e5b4a): siguen llegando como Unknown.
inline constexpr std::array<std::uint8_t, 13> WindowSubcodes{
    StateSubcode,    RegisterSubcode,   GiveUpSubcode,      RegistrationSubcode, RegisterMarkSubcode,
    NpcBuySubcode,   NpcRepairSubcode,  NpcUpgradeSubcode,  TaxInfoSubcode,      TaxRateSubcode,
    MoneyOutSubcode, SiegeFlagsSubcode, HuntZoneGateSubcode};

// Los nombres como los copian 0x64b0f0, 0x46f7e0 y 0x470bb0 (strncpy de 8 y de 10, sin terminador en el paquete). Son
// los mismos tipos que Protocol::Guild del hito 8 (GuildName, CharacterName): se unifican al juntar la cadena.
inline constexpr std::size_t GuildNameSize = 8;
inline constexpr std::size_t CharacterNameSize = 10;
using GuildName = std::array<std::byte, GuildNameSize>;
using CharacterName = std::array<std::byte, CharacterNameSize>;

// 0x4e5b4a: el resultado que pide cada caso, pkt[4].
inline constexpr std::uint8_t ResultFailed = 0;
inline constexpr std::uint8_t ResultOk = 1;

// B2 00 (0x4e50bd, tabla 0x4e61c4): el estado del castillo. Con 1 o 2, 0x64b0f0 lee pkt[5..0x2d].
struct SiegeDate
{
    std::uint16_t year = 0;             // pkt[6..7] (y +0xc, +0x12), el primero arriba
    std::array<std::uint8_t, 4> rest{}; // pkt[8..0xb] (y +0xe..+0x11, +0x14..+0x17)
};
struct CastleState
{
    std::uint8_t result = 0;
    std::int8_t state = 0;            // pkt[5], MOVSX
    std::array<SiegeDate, 3> dates{}; // pkt[6..0x17]
    GuildName ownerGuild{};           // pkt[0x18..0x1f]
    CharacterName ownerMaster{};      // pkt[0x20..0x29]
    std::uint32_t remaining = 0;      // pkt[0x2a..0x2d], el primero arriba
};

// B2 01 (0x4df080, 0..8) y B2 02 (0x4df1a0, 0..3): solo el resultado.
struct RegisterResult
{
    std::uint8_t result = 0;
};
struct GiveUpResult
{
    std::uint8_t result = 0;
};

// B2 03 (0x4e5186): con 1, pkt[0x11] (0x64d290 con pkt[0x11] == 0) y las marcas de pkt[0xd..0x10], el primero arriba.
struct RegistrationInfo
{
    std::uint8_t result = 0;
    std::uint32_t marks = 0;
    std::uint8_t gaveUp = 0;
};

// B2 04 (0x4e51fa, tabla 0x4e61d8): con 1, las marcas de pkt[0xd..0x10].
struct RegisterMarkResult
{
    std::uint8_t result = 0;
    std::uint32_t marks = 0;
};

// B2 05 (0x4df7d0), 06 (0x4e52b7) y 07 (0x4df950): con 1, el tipo (pkt+8), el índice (pkt+0xc) y lo que cambia, dwords
// tal cual (MOV r32: little-endian).
struct NpcBuyResult
{
    std::uint8_t result = 0;
    std::uint32_t type = 0;
    std::uint32_t index = 0;
};
struct NpcRepairResult
{
    std::uint8_t result = 0;
    std::uint32_t type = 0;
    std::uint32_t index = 0;
    std::uint32_t life = 0;    // pkt+0x10, al +0x14 del registro
    std::uint32_t maxLife = 0; // pkt+0x14, al +0x10 del registro
};
struct NpcUpgradeResult
{
    std::uint8_t result = 0;
    std::uint32_t type = 0;
    std::uint32_t index = 0;
    std::uint32_t kind = 0; // pkt+0x10: 1 la defensa, 2 la recuperación, 3 la vida
    std::uint32_t value = 0;
};

// B2 08 (0x4e537b → 0x669070): con 1, los dos impuestos (pkt[5] y pkt[6]) y la plata del castillo, pkt[7..0xe] con el
// primero arriba.
struct TaxInfo
{
    std::uint8_t result = 0;
    std::uint8_t chaosTax = 0;
    std::uint8_t storeTax = 0;
    std::uint64_t money = 0;
};

// B2 09 (0x4e53e7): con 1, cuál (pkt[5]: 1 y 2 los impuestos, 0x6690f0; 3 el precio de la zona de caza) y el valor de
// pkt[6..9], el primero arriba.
struct TaxRateResult
{
    std::uint8_t result = 0;
    std::uint8_t kind = 0;
    std::uint32_t value = 0;
};

// B2 10 (0x4e548a → 0x669160): con 1, la plata del castillo, pkt[5..0xc] con el primero arriba.
struct MoneyOutResult
{
    std::uint8_t result = 0;
    std::uint64_t money = 0;
};

// B2 1A (0x4e54f6): pkt[4] en 1 escribe 0x78aba34 y en 2 0x78aba30 con pkt[5].
struct SiegeFlags
{
    std::uint8_t which = 0;
    std::uint8_t value = 0;
};

// B2 1F (0x4e5532): con 1, la entrada a la zona de caza (pkt[5] distinto de 0).
struct HuntZoneGateResult
{
    std::uint8_t result = 0;
    std::uint8_t allowed = 0;
};

// B3 (0x4dfc60): con pkt[4] en 1, la cuenta (int en pkt+8; el lazo con JL no corre con 0 ni negativa) y los
// registros de 0x1c desde pkt+0xc, que 0x668fd0 copia enteros.
inline constexpr std::size_t NpcRecordSize = 0x1c;
using NpcRecord = std::array<std::byte, NpcRecordSize>;
struct NpcList
{
    std::uint8_t result = 0;
    std::vector<NpcRecord> records;
};

// B4 (0x4dfce0): con pkt[4] en 1, registros de 0xe desde pkt+0xc: el nombre, las marcas (pkt[8..0xb] del registro, el
// primero arriba) y dos bytes, como los pasa a 0x64d2c0.
struct SiegeGuildEntry
{
    GuildName name{};
    std::uint32_t marks = 0;
    std::uint8_t first = 0;  // +0xc
    std::uint8_t second = 0; // +0xd
};
struct SiegeGuildList
{
    std::uint8_t result = 0;
    std::vector<SiegeGuildEntry> entries;
};

// B5 (0x4dfd80, tabla 0x4dfe24): con pkt[4] en 1, registros de 10 desde pkt+0xc: dos bytes y el nombre, como los pasa
// a 0x64d310.
struct RegisteredGuildEntry
{
    std::uint8_t first = 0;  // +0
    std::uint8_t second = 0; // +1
    GuildName name{};        // +2
};
struct RegisteredGuildList
{
    std::uint8_t result = 0;
    std::vector<RegisteredGuildEntry> entries;
};

// B6 (0x4e0ed0): la cuenta (int en pkt+4) y los pares (x, y) desde pkt+8.
struct MinimapPoint
{
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};
struct MinimapPoints
{
    std::vector<MinimapPoint> points;
};

// B8 01 (0x4e0700): pkt[4], que en un C2 es el mismo subcódigo.
struct KillCount
{
    std::uint8_t value = 0;
};

using CastleSiegePacket =
    std::variant<CastleState, RegisterResult, GiveUpResult, RegistrationInfo, RegisterMarkResult, NpcBuyResult,
                 NpcRepairResult, NpcUpgradeResult, TaxInfo, TaxRateResult, MoneyOutResult, SiegeFlags,
                 HuntZoneGateResult, NpcList, SiegeGuildList, RegisteredGuildList, MinimapPoints, KillCount>;

// El paquete del castillo que es, por su opcode y, en el B2 y el B8, su subcódigo. Otro opcode, un B2 de la batalla o
// un B8 con otro subcódigo: WrongPacket.
[[nodiscard]] Core::Expected<CastleSiegePacket, ProtocolError> ReadCastleSiegePacket(const Network::Packet& packet);

// Lo que mandan las ventanas: el escritor de 0x6b85f4 en la pila, send() en claro por el socket del juego, y el XOR32
// desde el byte 3 a medida que pone cada campo.
// 0x64c0b0 (el botón con el estado 1): C1 04 B2 01.
[[nodiscard]] Outgoing WriteRegister();
// 0x64ccd0 (la respuesta 8 del diálogo): C1 05 B2 02 01.
[[nodiscard]] Outgoing WriteGiveUp();
// 0x64ae10 (abrir el 0x80000 con +4 en 0) y 0x4df1a0 (el B2 02 con 1): C1 04 B2 03.
[[nodiscard]] Outgoing WriteRegistrationRequest();
// 0x64c0b0 (el botón con el estado 3): C1 05 B2 04 y el lugar de la marca en el inventario (0x64b9d0).
[[nodiscard]] Outgoing WriteRegisterMark(std::uint8_t slot);
// 0x6623e0 y 0x664370 (la respuesta 8): C1 0C B2 05 con +0x1a del registro en 0 y C1 0C B2 06 con otro valor; el tipo y
// el índice del registro, dwords tal cual.
[[nodiscard]] Outgoing WriteNpcBuy(std::uint32_t type, std::uint32_t index);
[[nodiscard]] Outgoing WriteNpcRepair(std::uint32_t type, std::uint32_t index);
// 0x662d50 y 0x664ce0 (3, la vida), 0x663440 y 0x6653d0 (1, la defensa) y 0x665ab0 (2, la recuperación): C1 14 B2 07
// con el tipo, el índice, cuál y el valor nuevo, cuatro dwords tal cual.
[[nodiscard]] Outgoing WriteNpcUpgrade(std::uint32_t type, std::uint32_t index, std::uint32_t kind,
                                       std::uint32_t value);
// 0x661310 (abrir el 0x100000): C1 04 B2 08.
[[nodiscard]] Outgoing WriteTaxInfoRequest();
// 0x666b30 (1 y 2, los impuestos) y 0x648a30 (3, el precio de la zona de caza): C1 09 B2 09 con cuál y el valor, el
// byte de arriba primero (SAR 0x18, 0x10, 8 y el de abajo).
[[nodiscard]] Outgoing WriteTaxRate(std::uint8_t kind, std::uint32_t value);
// 0x667a40 (la respuesta 2): C1 08 B2 10 con lo que se saca, el byte de abajo primero (BL, BH y el word de arriba).
[[nodiscard]] Outgoing WriteMoneyOut(std::uint32_t amount);
// 0x648a30: C1 05 B2 1F con +0x15 ^ 1 (0x648c55).
[[nodiscard]] Outgoing WriteHuntZoneGate(std::uint8_t allowed);
// 0x661310: C1 04 B3 01 y C1 04 B3 02.
[[nodiscard]] Outgoing WriteNpcListRequest(std::uint8_t kind);
// 0x64d350 y 0x4df1a0: C1 03 B4; 0x64d350: C1 03 B5. Sin campos, sin nada que cifrar.
[[nodiscard]] Outgoing WriteSiegeGuildListRequest();
[[nodiscard]] Outgoing WriteRegisteredGuildListRequest();
// 0x648a30 (+0x1c4, en los dos modos): C1 08 B9 05 con el precio de +0x18, el dword tal cual.
[[nodiscard]] Outgoing WriteHuntZoneEntry(std::uint32_t fee);
} // namespace Client::Protocol::CastleSiege
