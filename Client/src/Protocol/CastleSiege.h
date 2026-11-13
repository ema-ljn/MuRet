#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// CL9 hito 12 (04 § Castle Siege, las ventanas del castillo): lo que llega en B2..B6 y B8 y lo que mandan las
// ventanas del castillo. El GameServer 0.97 del laboratorio no tiene Castle Siege: cada byte sale del ASM del
// cliente (c32bbd5f), *estático*.
namespace Client::Protocol::CastleSiege
{
inline constexpr std::uint8_t CastleSiegeOpcode = 0xb2;
inline constexpr std::uint8_t NpcListOpcode = 0xb3;
inline constexpr std::uint8_t GuildListOpcode = 0xb4;
inline constexpr std::uint8_t RegisteredGuildsOpcode = 0xb5;
inline constexpr std::uint8_t BattleGuildsOpcode = 0xb6;
inline constexpr std::uint8_t SkillOpcode = 0xb8;
inline constexpr std::uint8_t HuntZoneOpcode = 0xb9;

// Los subcódigos del B2 que manda la ventana.
inline constexpr std::uint8_t RegisterSubcode = 0x01;
inline constexpr std::uint8_t GiveUpSubcode = 0x02;
inline constexpr std::uint8_t RegistrationInfoSubcode = 0x03;
inline constexpr std::uint8_t RegisterMarkSubcode = 0x04;
inline constexpr std::uint8_t NpcBuySubcode = 0x05;
inline constexpr std::uint8_t NpcRepairSubcode = 0x06;
inline constexpr std::uint8_t NpcUpgradeSubcode = 0x07;
inline constexpr std::uint8_t CastleInfoSubcode = 0x08;
inline constexpr std::uint8_t TaxRateSubcode = 0x09;
inline constexpr std::uint8_t MoneyOutSubcode = 0x10;
inline constexpr std::uint8_t HuntZoneGateSubcode = 0x1f;
// B9 05: la entrada a la zona de caza del guardia (0x648a30).
inline constexpr std::uint8_t HuntZoneEntrySubcode = 0x05;
// Los cierres de las ventanas de NPC: C1 03 31 (la charla) y C1 03 87 (la mezcla del caos).
inline constexpr std::uint8_t CloseTalkOpcode = 0x31;
inline constexpr std::uint8_t CloseMixOpcode = 0x87;

// Lo que llega con B2, B3, B4, B5, B6 o B8, entero: los handlers del original (0x4e2b10 y los suyos) leen pkt[n] fijo
// desde el primer byte, sin mirar el largo, y el subcódigo del B2 y del B8 es pkt[3] en un C1 y pkt[4] en un C2.
// MuRet lee lo mismo; lo que el original leería pasado el paquete es el error TooShort de quien lo lee.
struct CastlePacket
{
    std::uint8_t opcode = 0;
    Network::PacketType type = Network::PacketType::C1;
    std::vector<std::byte> bytes;

    // 0x4e2b10: pkt[3] en un C1/C3, pkt[4] en un C2/C4.
    [[nodiscard]] Core::Expected<std::uint8_t, ProtocolError> Subcode() const;
    [[nodiscard]] Core::Expected<std::uint8_t, ProtocolError> U8(std::size_t at) const;
    // MOVSX de un byte.
    [[nodiscard]] Core::Expected<std::int8_t, ProtocolError> S8(std::size_t at) const;
    // Un dword como lo lee MOV r32,[pkt+at]: little-endian.
    [[nodiscard]] Core::Expected<std::uint32_t, ProtocolError> U32Le(std::size_t at) const;
    // Los cuatro bytes armados de a uno, el primero arriba (0x4df0d2, 0x6690f0).
    [[nodiscard]] Core::Expected<std::uint32_t, ProtocolError> U32Be(std::size_t at) const;
    // 0x669070 y 0x669160: los ocho bytes al revés en 0x7d5c650..0x7d5c657.
    [[nodiscard]] Core::Expected<std::uint64_t, ProtocolError> U64Be(std::size_t at) const;
    // `count` bytes desde `at`.
    [[nodiscard]] Core::Expected<std::span<const std::byte>, ProtocolError> Bytes(std::size_t at,
                                                                                  std::size_t count) const;
};

// El paquete de `opcode`: WrongPacket si es otro.
[[nodiscard]] Core::Expected<CastlePacket, ProtocolError> ReadCastlePacket(const Network::Packet& packet,
                                                                           std::uint8_t opcode);

// 0x4dfc60 (B3 con pkt[4] en 1): la cuenta (dword pkt+8) y los registros de 0x1c desde pkt+0xc, tal cual (0x668fd0
// copia los siete dwords).
struct NpcRecord
{
    std::array<std::byte, 0x1c> bytes{};
};
[[nodiscard]] Core::Expected<std::vector<NpcRecord>, ProtocolError> ReadNpcRecords(const CastlePacket& packet);

// 0x4dfce0 (B4 con pkt[4] en 1): registros de 0xe desde pkt+0xc: el nombre (8 bytes), las marcas (pkt[8..0xb],
// el primero arriba) y dos bytes (0x46f7e0 los guarda en +0x14 y +0x15 de la fila).
struct GuildListEntry
{
    std::array<char, 8> name{};
    std::int32_t marks = 0;
    std::uint8_t first = 0;
    std::uint8_t second = 0;
};
[[nodiscard]] Core::Expected<std::vector<GuildListEntry>, ProtocolError> ReadGuildList(const CastlePacket& packet);

// 0x4dfd80 (B5 con pkt[4] en 1): registros de 10 desde pkt+0xc: dos bytes y el nombre (8 bytes).
struct RegisteredGuildEntry
{
    std::uint8_t first = 0;
    std::uint8_t second = 0;
    std::array<char, 8> name{};
};
[[nodiscard]] Core::Expected<std::vector<RegisteredGuildEntry>, ProtocolError>
ReadRegisteredGuilds(const CastlePacket& packet);

// Lo que mandan las ventanas. Todos se arman con el escritor de 0x6b85f4 en la pila y salen con send() en claro; los
// que llevan algo después del opcode pasan por el XOR32 desde el byte 3.
// 0x64c0b0 (la pestaña 1 en el estado 1): C1 04 B2 01.
[[nodiscard]] Outgoing WriteRegister();
// 0x64ccd0 con la respuesta 8: C1 05 B2 02 01.
[[nodiscard]] Outgoing WriteGiveUp();
// 0x64ae10 (abrir el 0x80000 cerrado) y 0x4df1a0 (el B2 02 con 1): C1 04 B2 03.
[[nodiscard]] Outgoing WriteRegistrationInfoRequest();
// 0x64c0b0 (la pestaña 1 en el estado 3): C1 05 B2 04 y el lugar de la marca en el inventario.
[[nodiscard]] Outgoing WriteRegisterMark(std::uint8_t slot);
// 0x6623e0 y 0x664370: C1 0C B2 05 (sin el NPC, +0x1a en 0) o B2 06 con el tipo y el índice del registro, los dos
// dwords tal cual (little-endian).
[[nodiscard]] Outgoing WriteNpcBuy(std::uint32_t type, std::uint32_t index);
[[nodiscard]] Outgoing WriteNpcRepair(std::uint32_t type, std::uint32_t index);
// 0x662d50, 0x663440, 0x664ce0, 0x6653d0 y 0x665ab0: C1 14 B2 07 con el tipo, el índice, qué sube (1 la defensa, 2 la
// recuperación, 3 la vida) y el valor nuevo, cuatro dwords little-endian.
[[nodiscard]] Outgoing WriteNpcUpgrade(std::uint32_t type, std::uint32_t index, std::uint32_t kind,
                                       std::uint32_t value);
// 0x661310 (abrir el 0x100000 cerrado): C1 04 B2 08.
[[nodiscard]] Outgoing WriteCastleInfoRequest();
// 0x666b30 (1 y 2, los impuestos) y 0x648a30 (3, la entrada a la zona de caza): C1 08 B2 09 con el tipo y el valor,
// el byte de arriba primero (SAR 0x18, 0x10, 8 y el de abajo).
[[nodiscard]] Outgoing WriteTaxRate(std::uint8_t kind, std::uint32_t value);
// 0x667a40 con la respuesta 2: C1 08 B2 10 con lo que se saca, little-endian (BL, BH y el word de arriba).
[[nodiscard]] Outgoing WriteMoneyOut(std::uint32_t amount);
// 0x648a30 (el clic en la línea del modo 3): C1 05 B2 1F con +0x15 ^ 1.
[[nodiscard]] Outgoing WriteHuntZoneGate(std::uint8_t allowed);
// 0x661310: C1 04 B3 01 (las puertas) y C1 04 B3 02 (las estatuas).
[[nodiscard]] Outgoing WriteNpcListRequest(std::uint8_t kind);
// 0x64d350 (la pestaña 2 en los estados 1 y 3) y 0x4df1a0: C1 03 B4, sin XOR32 (no tiene byte 3).
[[nodiscard]] Outgoing WriteGuildListRequest();
// 0x64d350 (la pestaña 2 en los estados 5 y 6): C1 03 B5.
[[nodiscard]] Outgoing WriteRegisteredGuildsRequest();
// 0x648a30 (el botón de +0x1c4): C1 08 B9 05 con el precio de +0x18, el dword tal cual.
[[nodiscard]] Outgoing WriteHuntZoneEntry(std::uint32_t fee);
// 0x64b200, 0x6487d0 y 0x661bc0: C1 03 31; 0x661bc0 con +0x3d4: C1 03 87.
[[nodiscard]] Outgoing WriteCloseTalk();
[[nodiscard]] Outgoing WriteCloseMix();
} // namespace Client::Protocol::CastleSiege
