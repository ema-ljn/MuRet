#pragma once

#include "Network/Packet.h"
#include "Network/ProtocolError.h"

#include "support/ExpectedError.h"
#include "support/Packets.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

// A reader's layout reads exactly what the original's reader reads: a packet of that size reads and one byte less is
// TooShort (the AR2 plan's Review Focus).
namespace Client::Tests
{
// `bytes` without its last byte, its C1 or C2 length rewritten.
inline std::vector<std::byte> OneByteShorter(std::vector<std::byte> bytes)
{
    bytes.pop_back();
    const auto size = static_cast<std::uint16_t>(bytes.size());
    if (bytes.front() == std::byte{0xc2} || bytes.front() == std::byte{0xc4})
    {
        bytes[1] = static_cast<std::byte>(size >> 8);
        bytes[2] = static_cast<std::byte>(size & 0xff);
    }
    else
    {
        bytes[1] = static_cast<std::byte>(size);
    }
    return bytes;
}

template <typename Read> void ExpectMinimumSize(Read read, const std::vector<std::byte>& exact)
{
    EXPECT_TRUE(read(PacketOf(exact)).HasValue());
    EXPECT_EQ(ErrorOf(read(PacketOf(OneByteShorter(exact)))), Network::ProtocolError::TooShort);
}
} // namespace Client::Tests
