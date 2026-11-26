#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Protocol/Outgoing.h"

#include <cstdint>

// The duel (04, "El duelo"): the AA to the AD both ways. The GameServer 0.97 does not have them (08 P-C200): the oracle
// is the client's ASM. The readers are the fields 0x4dac90 (AA) and the dispatcher 0x4e2b10's cases (AB 0x4e4d10, AC
// 0x4e4d75, AD 0x4e4dd1) read; the writers, what 0x406050 (AA and AB), 0x56c440 (AA and AB) and 0x5ebca0 (AC) build.
namespace Client::Protocol::Social
{
// The GS's AA (0x4dac90): C1 10 AA, the result at pkt[3] (other than 0, it started), the rival's key at pkt[4..5]
// (big-endian) and their name at pkt[6..15]. With 0 it only reads the name.
struct DuelStart
{
    std::uint8_t result = 0;
    std::uint16_t key = 0;
    FrontEnd::NameBytes name{};
};

[[nodiscard]] Core::Expected<DuelStart, Network::ProtocolError> ReadDuelStart(const Network::Packet& packet);

// The GS's AB (0x4e4d10), the end: C1 0F AB, the key at pkt[3..4] (big-endian) and the name at pkt[5..14].
struct DuelEnd
{
    std::uint16_t key = 0;
    FrontEnd::NameBytes name{};
};

[[nodiscard]] Core::Expected<DuelEnd, Network::ProtocolError> ReadDuelEnd(const Network::Packet& packet);

// The GS's AC (0x4e4d75), another's request: the same fields as the AB.
struct DuelRequest
{
    std::uint16_t key = 0;
    FrontEnd::NameBytes name{};
};

[[nodiscard]] Core::Expected<DuelRequest, Network::ProtocolError> ReadDuelRequest(const Network::Packet& packet);

// The AD (0x4e4dd1): C1 09 AD, two keys (pkt[3..4] and pkt[5..6], big-endian) and their two scores (pkt[7] and
// pkt[8]).
struct DuelScore
{
    std::uint16_t firstKey = 0;
    std::uint16_t secondKey = 0;
    std::uint8_t firstScore = 0;
    std::uint8_t secondScore = 0;
};

[[nodiscard]] Core::Expected<DuelScore, Network::ProtocolError> ReadDuelScore(const Network::Packet& packet);

// The client's AA (0x406050 from 0x4079e3; /duelstart at 0x56c440): C1 0F AA, the other's key big-endian and their
// name (+0x1d9, 10 bytes), with the XOR32 and as a C3.
[[nodiscard]] Outgoing WriteDuelStart(std::uint16_t key, const FrontEnd::NameBytes& name);

// The client's AB (0x406050 from 0x408141; /duelend at 0x56c440): C1 03 AB, as a C3.
[[nodiscard]] Outgoing WriteDuelEnd();

// The client's AC (0x5ebca0: yes 0x5ebd3f, no 0x5ec55d): C1 10 AC, the answer (1 yes, 0 no), 0x78abadc's key
// big-endian and 0x78abae0's name, with the XOR32 and as a C3.
[[nodiscard]] Outgoing WriteDuelAnswer(std::uint8_t accepted, std::uint16_t key, const FrontEnd::NameBytes& name);
} // namespace Client::Protocol::Social
