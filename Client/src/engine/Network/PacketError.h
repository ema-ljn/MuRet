#pragma once

#include <cstdint>

// 03-red-y-cifrado.md, "Cadena de recepción". InvalidType, SizeTooSmall and TooLarge fix three bugs of the original:
// Q-C38, Q-C39 and Q-C40 (07-quirks-y-bugs.md).
namespace Client::Network
{
enum class PacketError : std::uint8_t
{
    InvalidType,  // not C1, C2, C3 or C4
    SizeTooSmall, // below the header and the opcode: 3 bytes in C1 and C3, 4 in C2 and C4
    SizeMismatch,
    TooLarge, // over MaxPacketSize (Q-C40); when writing, over what the type holds (Q-C06)
};
} // namespace Client::Network
