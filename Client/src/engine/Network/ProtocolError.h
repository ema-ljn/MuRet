#pragma once

#include <cstdint>

namespace Client::Network
{
enum class ProtocolError : std::uint8_t
{
    WrongPacket,  // another type, opcode or subcode than the packet being read
    TooShort,     // fields missing, or the list says more entries than it carries
    FieldTooLong, // when writing, a text that does not fit its field (the login's account or password)
};
} // namespace Client::Network
