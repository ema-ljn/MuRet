#include "Network/PacketCode.h"

#include "Network/Packet.h"

#include <gtest/gtest.h>

namespace
{
using Client::Network::MinPacketSize;
using Client::Network::PacketType;
using Client::Network::SubcodeOffset;

TEST(PacketCodeTest, PutsTheSubcodeRightAfterTheOpcode)
{
    EXPECT_EQ(SubcodeOffset(PacketType::C1), 3U);
    EXPECT_EQ(SubcodeOffset(PacketType::C3), 3U);
    EXPECT_EQ(SubcodeOffset(PacketType::C2), 4U);
    EXPECT_EQ(SubcodeOffset(PacketType::C4), 4U);
}

TEST(PacketCodeTest, PutsTheSubcodeWhereTheHeaderAndTheOpcodeEnd)
{
    for (const PacketType type : {PacketType::C1, PacketType::C2, PacketType::C3, PacketType::C4})
    {
        EXPECT_EQ(SubcodeOffset(type), MinPacketSize(type));
    }
}
} // namespace
