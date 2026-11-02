// Armar un Packet en un test con bytes escritos a mano.

#pragma once

#include "Network/Packet.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <utility>
#include <vector>

namespace Client::Tests
{

// Un Packet con estos bytes. Si no lo forman, el test falla (y Value() corta el programa): es un
// error del literal, no del código.
inline Network::Packet PacketOf(std::vector<std::byte> bytes)
{
    auto built = Network::Packet::FromBytes(std::move(bytes));
    EXPECT_TRUE(built.HasValue());
    return std::move(built).Value();
}

} // namespace Client::Tests
