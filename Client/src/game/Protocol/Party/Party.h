#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Protocol/Outgoing.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// The party (04, "La party"): the 40 to the 44 both ways (08 P-C92). The readers are the fields the dispatcher
// 0x4e2b10's cases (0x4e3f41..0x4e41ba) and 0x4d67e0 read; the writers, what 0x6599a0(4) (42), 0x406050 and 0x56c440
// (40), 0x623e70 (41) and 0x5e3c80 (43) build.
namespace Client::Protocol::Party
{
// 0x4d67e0: each member takes 0x18 bytes from pkt[5].
inline constexpr std::size_t MemberWireSize = 0x18;

// 0x659aa6..0x659b94: C1 03 42 built on the stack and sent with send if 0x6c82a0's socket (+0xc) is not −1. It goes
// through neither the XOR32 nor the C3's encryption: it travels in the clear.
[[nodiscard]] Outgoing WritePartyListRequest();

// The GS's 40 (0x4e3f41): C1 05 40 and the inviter's key big-endian (pkt[3] << 8 | pkt[4]).
struct PartyRequest
{
    std::uint16_t key = 0;
};

[[nodiscard]] Core::Expected<PartyRequest, Network::ProtocolError> ReadPartyRequest(const Network::Packet& packet);

// The GS's 41 (0x4e3f6f): C1 04 41 and the result (0 to 5 a text; above, none).
struct PartyRequestResult
{
    std::uint8_t result = 0;
};

[[nodiscard]] Core::Expected<PartyRequestResult, Network::ProtocolError>
ReadPartyRequestResult(const Network::Packet& packet);

// A member of the 42, 0x18 bytes: the name (10), the number (+0xa), the map (+0xb), the cell (+0xc and +0xd), two
// bytes 0x4d67e0 does not copy, the life (+0x10) and the maximum life (+0x14), both DWORDs little-endian.
struct PartyMember
{
    FrontEnd::NameBytes name{};
    std::uint8_t number = 0;
    std::uint8_t map = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint32_t life = 0;
    std::uint32_t maxLife = 0;
};

// The GS's 42 (0x4d67e0): C1 ?? 42, the result at pkt[3] (which nobody reads), the count at pkt[4] and that many
// members from pkt[5]. 0x4d67e0 puts no cap on it: the reader reads them all, and whoever applies them stops past
// 0x77f3cd0's five places.
struct PartyList
{
    std::uint8_t result = 0;
    std::vector<PartyMember> members;
};

[[nodiscard]] Core::Expected<PartyList, Network::ProtocolError> ReadPartyList(const Network::Packet& packet);

// The GS's 43 (0x4e409b): C1 03 43, with no fields.
struct PartyLeft
{
};

[[nodiscard]] Core::Expected<PartyLeft, Network::ProtocolError> ReadPartyLeft(const Network::Packet& packet);

// The GS's 44 (0x4e4174): C1 ?? 44, the count at pkt[3] and one byte per member from pkt[4]: the place in the high
// nibble and the life in tenths in the low one.
struct PartyLife
{
    std::vector<std::uint8_t> entries;
};

[[nodiscard]] Core::Expected<PartyLife, Network::ProtocolError> ReadPartyLife(const Network::Packet& packet);

// The client's 40 (0x406050 from 0x40739b; 0x56c440 with /party): C1 05 40 and the other's key big-endian, with the
// XOR32 and as a C3.
[[nodiscard]] Outgoing WritePartyRequest(std::uint16_t key);

// The client's 41 (0x623e70's dialog 0x77: yes 0x62bb1d, no 0x62cd52): C1 06 41, the answer (1 yes, 0 no) and
// 0x78aba4c's key big-endian, with the XOR32 and as a C3.
[[nodiscard]] Outgoing WritePartyRequestResult(std::uint8_t accepted, std::uint16_t key);

// The client's 43 (0x5e3c80, 0x5e3ded..0x5e407b): C1 04 43 and the member's number (+0xb of its place in 0x77f3cd0),
// with the XOR32 and in the clear.
[[nodiscard]] Outgoing WritePartyLeave(std::uint8_t number);
} // namespace Client::Protocol::Party
