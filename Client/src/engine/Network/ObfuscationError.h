#pragma once

#include <cstdint>

// Contract: Client/docs/research/03-red-y-cifrado.md, "La capa de FoxAnti". InvalidPacket fixes the lengths the
// original does not check: Q-C09 in 07-quirks-y-bugs.md.
namespace Client::Network
{
enum class ObfuscationError : std::uint8_t
{
    // A length or count that does not fit the packet, or the translation would not fit. The connection drops that
    // packet and keeps reading: the framing is not lost, because the header gave the length.
    InvalidPacket,
};
} // namespace Client::Network
