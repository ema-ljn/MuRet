#include "Network/Connection.h"

#include "App/LabXorRemapTables.h"
#include "Crypto/SimpleModulus.h"
#include "Network/EncryptedPacket.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"
#include "Network/XorRemap/XorRemapObfuscation.h"

#include "support/NetworkPrinters.h"
#include "support/SameBytes.h"
#include "support/WireCapture.h"
#include "support/WirePackets.h"
#include "test_golden_connection_support.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

// Client::Network::Connection with the XOR and remap layer against the lab's wire capture (test_golden_connection.cpp
// has the rest, and what these tests may print).
namespace
{
using Client::App::LabXorRemapTables;
using Client::Network::Connection;
using Client::Network::DecryptPacket;
using Client::Network::Packet;
using Client::Network::PacketSerials;
using Client::Network::PacketType;
using Client::Network::SerialCheck;
using Client::Network::XorRemapObfuscation;
using Client::Network::Crypto::DecryptionKey;
using Client::Tests::CutPackets;
using Client::Tests::SameBytesWithoutValues;
using Client::Tests::WireDirection;
using Client::Tests::GoldenConnectionSupport::GameServerPort;
using Client::Tests::GoldenConnectionSupport::IsEncrypted;
using Client::Tests::GoldenConnectionSupport::Keys;
using Client::Tests::GoldenConnectionSupport::PaddingSeed;
using Client::Tests::GoldenConnectionSupport::Where;

using GoldenConnectionXorRemap = Client::Tests::GoldenConnectionSupport::GoldenConnection;

struct EncryptedCount
{
    std::size_t c3 = 0;
    std::size_t c4 = 0;
    std::size_t connections = 0;
};

// Each C3/C4 of `cut`, decrypted and passed through BeforeDispatch, against what the Connection gave in its place.
void ExpectDecryptedLikeDecryptPacket(const std::vector<Packet>& cut, const std::vector<Packet>& delivered,
                                      const DecryptionKey& decryption, const XorRemapObfuscation& xorRemap,
                                      std::size_t index, EncryptedCount& count)
{
    ASSERT_EQ(delivered.size(), cut.size()) << "connection " << index;
    PacketSerials serials;
    bool anyEncrypted = false;
    for (std::size_t packet = 0; packet < cut.size(); ++packet)
    {
        if (!IsEncrypted(cut[packet]))
        {
            continue;
        }
        anyEncrypted = true;
        if (cut[packet].Type() == PacketType::C3)
        {
            ++count.c3;
        }
        else
        {
            ++count.c4;
        }
        auto decrypted = DecryptPacket(cut[packet], decryption, serials);
        ASSERT_TRUE(decrypted.HasValue()) << Where(index, packet);
        EXPECT_EQ(decrypted.Value().serial, SerialCheck::InOrder) << Where(index, packet);
        auto expected = xorRemap.BeforeDispatch(std::move(decrypted).Value().packet);
        ASSERT_TRUE(expected.HasValue()) << Where(index, packet);
        EXPECT_TRUE(SameBytesWithoutValues(delivered[packet].Bytes(), expected.Value().Bytes()))
            << Where(index, packet);
    }
    count.connections += anyEncrypted ? 1 : 0;
}

// The GameServer's 25 answers come out as DecryptPacket with Dec2.dat and BeforeDispatch give them, serials from 0
// at each Open, with no Undecryptable and no warning. What arrived clear comes out too, in its place.
TEST_F(GoldenConnectionXorRemap, DeliversWhatTheGameServerAnswered)
{
    if (!m_keys)
    {
        FAIL() << "SetUp did not load the keys";
    }
    const Keys& keys = *m_keys;
    const XorRemapObfuscation xorRemap{LabXorRemapTables()};
    Connection connection(m_socket, xorRemap, m_clock, m_log, PaddingSeed);
    EncryptedCount count;

    for (std::size_t index = 0; index < m_connections.size(); ++index)
    {
        if (m_connections[index].port != GameServerPort)
        {
            continue;
        }
        const std::vector<std::span<const std::byte>> reads = m_connections[index].Reads(WireDirection::ServerToClient);
        const std::vector<Packet> cut = CutPackets(reads, xorRemap, index);
        ASSERT_NO_FATAL_FAILURE(OpenTheGameServer(connection, keys.client, index));
        for (const std::span<const std::byte> read : reads)
        {
            m_socket.reads.emplace_back(read.begin(), read.end());
        }
        const std::vector<Packet> delivered = PollEverything(connection, index);
        ASSERT_NO_FATAL_FAILURE(
            ExpectDecryptedLikeDecryptPacket(cut, delivered, keys.client.decryption, xorRemap, index, count));
    }
    EXPECT_EQ(count.c3, 18u);
    EXPECT_EQ(count.c4, 7u);
    EXPECT_EQ(count.connections, 7u);
    EXPECT_EQ(WarningCount(), 0u);
}
} // namespace
