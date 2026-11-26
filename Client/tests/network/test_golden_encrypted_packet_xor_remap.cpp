#include "App/LabXorRemapTables.h"
#include "Network/Packet.h"
#include "Network/XorRemap/XorRemapObfuscation.h"

#include "support/WireCapture.h"
#include "test_golden_encrypted_packet_support.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// What the lab's GameServer answered, cut with the XOR and remap layer (test_golden_encrypted_packet.cpp has the
// rest, and what these tests may print).
namespace
{
using Client::App::LabXorRemapTables;
using Client::Network::PacketType;
using Client::Network::XorRemap::XorRemapObfuscation;
using Client::Tests::WireDirection;
using Client::Tests::GoldenEncryptedPacketSupport::CountOf;
using Client::Tests::GoldenEncryptedPacketSupport::DecryptedCounts;
using Client::Tests::GoldenEncryptedPacketSupport::ExpectSameAsItsBlocks;
using Client::Tests::GoldenEncryptedPacketSupport::WirePacket;

using GoldenEncryptedPacketXorRemap = Client::Tests::GoldenEncryptedPacketSupport::GoldenEncryptedPacket;

// The GameServer's 25 answers (18 C3 and 7 C4, of 36, 69 and 80 bytes) in 7 connections: serials from 0, the rebuilt
// C1/C2 equal to the useful bytes of the blocks, and the blocks, encrypted again with Enc2.dat, the same bytes.
TEST_F(GoldenEncryptedPacketXorRemap, DecryptsWhatTheGameServerAnswered)
{
    const XorRemapObfuscation xorRemap{LabXorRemapTables()};
    const std::vector<WirePacket> answered = DecryptAll(WireDirection::ServerToClient, xorRemap, *m_dec2);

    ExpectSameAsItsBlocks(answered, *m_dec2, *m_enc2);
    const DecryptedCounts counts = CountOf(answered);
    EXPECT_EQ(counts.c3, 18u);
    EXPECT_EQ(counts.c4, 7u);
    EXPECT_EQ(counts.connections, 7u);
}

// Seven captures against the C4/C2 of the independent Python reference. DecryptAll goes through every C3/C4 first:
// filtering before would lose the earlier serials.
TEST_F(GoldenEncryptedPacketXorRemap, ReadsAllSevenInventoryCapturesAgainstTheIndependentReference)
{
    const XorRemapObfuscation xorRemap{LabXorRemapTables()};
    const std::vector<WirePacket> answered = DecryptAll(WireDirection::ServerToClient, xorRemap, *m_dec2);
    ASSERT_EQ(answered.size(), 25u);
    std::size_t found = 0;
    std::array<std::size_t, 7> counts{};
    for (const WirePacket& packet : answered)
    {
        ASSERT_TRUE(packet.plain.has_value());
        if (packet.plain->Type() != PacketType::C2 || packet.plain->Opcode() != Client::Network::Opcode{0xF3})
        {
            continue;
        }
        const std::span<const std::byte> bytes = packet.plain->Bytes();
        ASSERT_GE(bytes.size(), 5u);
        if (bytes[4] != std::byte{0x10})
        {
            continue;
        }
        SCOPED_TRACE(found);
        ASSERT_LT(found, 7u);
        ASSERT_EQ(packet.encrypted.Type(), PacketType::C4);
        ASSERT_GE(bytes.size(), 6u);
        const auto count = std::to_integer<std::uint8_t>(bytes[5]);
        ASSERT_LT(count, counts.size());
        ASSERT_NO_FATAL_FAILURE(ExpectInventoryReference(packet, found));
        ++counts[count];
        ++found;
    }
    EXPECT_EQ(found, 7u);
    EXPECT_EQ(counts[2], 1u);
    EXPECT_EQ(counts[5], 5u);
    EXPECT_EQ(counts[6], 1u);
}
} // namespace
