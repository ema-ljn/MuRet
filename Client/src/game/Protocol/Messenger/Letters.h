#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/Outgoing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The letters (04, "Las cartas y el chat con un amigo"): what arrives (C5, C6 and C8 in a C1, C7 in a C2; 0x4dc840,
// 0x4dc9e0, 0x4dcb70 and 0x4dcd90) and what the letters' windows send (the writing one, 0x498820, and the letters tab,
// 0x4a11b0). The GameServer 0.97 reconstruction has no letters: the oracle is the client's ASM. The dispatcher
// (0x4e2b10) has no case for the C9.
namespace Client::Protocol::Messenger
{
// The name in 10 bytes, the subject in 0x20 and the body up to 1000 (0x3e8).
inline constexpr std::size_t LetterNameSize = 10;
inline constexpr std::size_t LetterSubjectSize = 0x20;
inline constexpr std::size_t LetterBodyLimit = 1000;
// C6: the date (pkt[0x10], strncpy of 10) and the time (pkt[0x1b], strncpy of 8).
inline constexpr std::size_t LetterDateSize = 10;
inline constexpr std::size_t LetterTimeSize = 8;
// C7: the portrait's equipment (pkt[9..0x19]), like 0x5139f0's.
inline constexpr std::size_t PortraitEquipmentSize = 17;
// C7: 0x4a03c0 keeps 0x404 bytes of the packet (the 0x1c header and the 1000 of the body).
inline constexpr std::size_t LetterCopySize = 0x404;

// C1 C5 (0x4dc840): pkt[3] the result and pkt[4..7] the writing window (+0x40).
struct LetterSendResult
{
    std::uint8_t result = 0;
    std::uint32_t window = 0;
};

// C1 C6 (0x4dc9e0): pkt[4] the number (u16), pkt[6] the sender, pkt[0x10] the date, pkt[0x1b] the time, pkt[0x2e]
// the subject and pkt[0x4e] the type (0 and 1, one of the list; 2, one arriving now; more, none).
struct LetterRow
{
    std::uint16_t index = 0;
    std::string name;
    std::string date;
    std::string time;
    std::string subject;
    std::uint8_t type = 0;
    friend bool operator==(const LetterRow&, const LetterRow&) = default;
};

// C2 C7 (0x4dcb70): pkt[4] the number, pkt[6] the length (u16), pkt[8] the portrait's class, pkt[9..0x19] its
// equipment, pkt[0x1a] the direction (the low 6 bits, the angle; the high 2, the zoom), pkt[0x1b] the action and from
// pkt[0x1c] the body. The original puts a 0 at pkt[0x1c + length] without looking at the size (Q-C1000): MuRet rejects
// a length that does not fit in the packet.
struct LetterRead
{
    std::uint16_t index = 0;
    std::uint16_t length = 0;
    std::uint8_t portraitClass = 0;
    std::array<std::byte, PortraitEquipmentSize> equipment{};
    std::uint8_t direction = 0;
    std::uint8_t action = 0;
    // strncpy of 1000 from pkt[0x1c] with the 0 at the length: up to the first '\0'.
    std::string body;
    // What 0x4a03c0 keeps: the packet's first 0x404 bytes (fewer, if it is shorter), with the length's 0.
    std::vector<std::byte> copy;
};

// C1 C8 (0x4dcd90): pkt[3] the result and pkt[4] the number (u16).
struct LetterDeleteResult
{
    std::uint8_t result = 0;
    std::uint16_t index = 0;
};

// What the writing window builds with 10 and 1 (0x498820): the window, the addressee, the subject, the portrait's
// direction and action and the body already converted (without '\0').
struct LetterToSend
{
    std::uint32_t window = 0;
    std::string name;
    std::string subject;
    std::uint8_t direction = 0;
    std::uint8_t action = 0;
    std::string body;
    friend bool operator==(const LetterToSend&, const LetterToSend&) = default;
};

[[nodiscard]] Core::Expected<LetterSendResult, Network::ProtocolError>
ReadLetterSendResult(const Network::Packet& packet);
[[nodiscard]] Core::Expected<LetterRow, Network::ProtocolError> ReadLetterRow(const Network::Packet& packet);
[[nodiscard]] Core::Expected<LetterRead, Network::ProtocolError> ReadLetterRead(const Network::Packet& packet);
// The copy of 0x4a03c0 that 0x4a11b0 (10 and 2) gives back to 0x4dcb70: the same places over the bytes.
[[nodiscard]] Core::Expected<LetterRead, Network::ProtocolError> ReadLetterCopy(std::span<const std::byte> bytes);
[[nodiscard]] Core::Expected<LetterDeleteResult, Network::ProtocolError>
ReadLetterDeleteResult(const Network::Packet& packet);

// 0x498820 with 10 and 1: C2 C5, the window (u32), the name (10), the subject (0x20), the direction, the action, the
// length (u16, up to 1000) and the body, with the XOR32 from byte 4, and it goes out encrypted as a C4
// (0x4992fe..0x499416).
[[nodiscard]] Outgoing WriteLetterSend(const LetterToSend& letter);
// 0x4a11b0 with 10 and 2 without the letter kept: C1 06 C7, a 0 and the number (u16), with the XOR32.
[[nodiscard]] Outgoing WriteLetterReadRequest(std::uint16_t index);
// 0x4a11b0 with 12 and 1: C1 06 C8, a 0 and the number of each marked letter, with the XOR32.
[[nodiscard]] Outgoing WriteLetterDelete(std::uint16_t index);
// 0x4a11b0 with 10 and 5: C1 03 C9 in the clear.
[[nodiscard]] Outgoing WriteLetterListRequest();

// The name or the subject in `size` bytes with zeros after, cut at the first '\0' (strncpy).
[[nodiscard]] std::vector<std::byte> FixedText(std::string_view text, std::size_t size);
} // namespace Client::Protocol::Messenger
