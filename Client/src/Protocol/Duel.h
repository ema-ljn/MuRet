#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <array>
#include <cstddef>
#include <cstdint>

// 04 § El duelo: del AA al AD en los dos sentidos (CL9 hito 7). El GameServer 0.97 no los tiene (08 P-C200): el
// oráculo es el ASM del cliente. Los lectores son los campos que leen 0x4dac90 (AA) y los casos del dispatcher
// 0x4e2b10 (AB 0x4e4d10, AC 0x4e4d75, AD 0x4e4dd1); los escritores, lo que arman 0x406050 (AA y AB), 0x56c440 (AA y AB)
// y 0x5ebca0 (AC).
namespace Client::Protocol::Duel
{
inline constexpr std::uint8_t StartOpcode = 0xaa;
inline constexpr std::uint8_t EndOpcode = 0xab;
inline constexpr std::uint8_t RequestOpcode = 0xac;
inline constexpr std::uint8_t ScoreOpcode = 0xad;

using Name = std::array<std::byte, Character::NameSize>;

// El AA del GS (0x4dac90): C1 10 AA, el resultado en pkt[3] (distinto de 0, empezó), la clave del rival en pkt[4..5]
// (big-endian) y su nombre en pkt[6..15]. Con 0 solo lee el nombre.
struct DuelStart
{
    std::uint8_t result = 0;
    std::uint16_t key = 0;
    Name name{};
};

[[nodiscard]] Core::Expected<DuelStart, ProtocolError> ReadDuelStart(const Network::Packet& packet);

// El AB del GS (0x4e4d10), el fin: C1 0F AB, la clave en pkt[3..4] (big-endian) y el nombre en pkt[5..14].
struct DuelEnd
{
    std::uint16_t key = 0;
    Name name{};
};

[[nodiscard]] Core::Expected<DuelEnd, ProtocolError> ReadDuelEnd(const Network::Packet& packet);

// El AC del GS (0x4e4d75), el pedido de otro: los mismos campos que el AB.
struct DuelRequest
{
    std::uint16_t key = 0;
    Name name{};
};

[[nodiscard]] Core::Expected<DuelRequest, ProtocolError> ReadDuelRequest(const Network::Packet& packet);

// El AD (0x4e4dd1): C1 09 AD, dos claves (pkt[3..4] y pkt[5..6], big-endian) y sus dos puntos (pkt[7] y pkt[8]).
struct DuelScore
{
    std::uint16_t firstKey = 0;
    std::uint16_t secondKey = 0;
    std::uint8_t firstScore = 0;
    std::uint8_t secondScore = 0;
};

[[nodiscard]] Core::Expected<DuelScore, ProtocolError> ReadDuelScore(const Network::Packet& packet);

// El AA del cliente (0x406050 desde 0x4079e3; /duelstart en 0x56c440): C1 0F AA, la clave del otro en big-endian y su
// nombre (+0x1d9, 10 bytes), con el XOR32 y en C3.
[[nodiscard]] Outgoing WriteDuelStart(std::uint16_t key, const Name& name);

// El AB del cliente (0x406050 desde 0x408141; /duelend en 0x56c440): C1 03 AB, en C3.
[[nodiscard]] Outgoing WriteDuelEnd();

// El AC del cliente (0x5ebca0: sí 0x5ebd3f, no 0x5ec55d): C1 10 AC, la respuesta (1 sí, 0 no), la clave de 0x78abadc
// en big-endian y el nombre de 0x78abae0, con el XOR32 y en C3.
[[nodiscard]] Outgoing WriteDuelAnswer(std::uint8_t accepted, std::uint16_t key, const Name& name);
} // namespace Client::Protocol::Duel
