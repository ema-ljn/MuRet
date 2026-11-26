#pragma once

#include "Network/Packet.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <utility>
#include <vector>

// Building a Packet in a test from hand-written bytes.
namespace Client::Tests
{
// If the bytes do not form a Packet, the test fails (and Value() aborts the program): it is an error of the literal,
// not of the code.
inline Network::Packet PacketOf(std::vector<std::byte> bytes)
{
    auto built = Network::Packet::FromBytes(std::move(bytes));
    EXPECT_TRUE(built.HasValue());
    return std::move(built).Value();
}
} // namespace Client::Tests
