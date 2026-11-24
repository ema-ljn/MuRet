#include "Network/Connection.h"

#include "App/LabXorRemapTables.h"
#include "Network/Delivery.h"
#include "Network/LinkState.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"
#include "Network/XorRemapObfuscation.h"
#include "Protocol/Connection/ServerList.h"
#include "Protocol/Dispatcher.h"
#include "Protocol/Outgoing.h"

#include "support/Bytes.h"
#include "support/NetworkPrinters.h"
#include "support/Packets.h"
#include "support/SameBytes.h"
#include "support/SessionPackets.h"
#include "support/TestGameServer.h"
#include "test_connection_support.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

// Client::Network::Connection with the XOR and remap layer of the lab server (test_connection.cpp has the rest).
namespace
{
using Client::App::LabXorRemapTables;
using Client::Network::Connection;
using Client::Network::Delivery;
using Client::Network::Event;
using Client::Network::LinkState;
using Client::Network::Packet;
using Client::Network::PacketSerials;
using Client::Network::XorRemapObfuscation;
using Client::Protocol::Connection::WriteServerSelect;
using Client::Tests::AsTheGameServerReceives;
using Client::Tests::Bytes;
using Client::Tests::FromTheGameServer;
using Client::Tests::GameServerReceived;
using Client::Tests::Hello;
using Client::Tests::Join;
using Client::Tests::PacketOf;
using Client::Tests::SameBytes;
using Client::Tests::ConnectionSupport::ByteVector;
using Client::Tests::ConnectionSupport::Describe;
using Client::Tests::ConnectionSupport::GameHost;
using Client::Tests::ConnectionSupport::GamePort;
using Client::Tests::ConnectionSupport::HexOf;
using Client::Tests::ConnectionSupport::Lines;
using Client::Tests::ConnectionSupport::NoKeys;
using Client::Tests::ConnectionSupport::PaddingSeed;

// The lab server's XOR on what it sends.
constexpr std::byte LabMask = LabXorRemapTables().inboundXor;

class ConnectionXorRemapTest : public Client::Tests::ConnectionSupport::ConnectionTest
{
protected:
    XorRemapObfuscation m_xorRemap{LabXorRemapTables()};
    Connection m_labConnection{m_socket, m_xorRemap, m_clock, m_log, PaddingSeed};
};

// 04 § El flujo de conexión: the lab's F4 02 list becomes the F4 06 that main.exe reads, with the code 400.
TEST_F(ConnectionXorRemapTest, TheListArrivesAsF406)
{
    OpenAndConnect(m_labConnection);
    m_socket.reads = {Bytes({0xC2, 0x00, 0x0A, 0xF4, 0x02, 0x01, 0x00, 0x00, 0x00, 0xCC})};

    EXPECT_EQ(Describe(m_labConnection.Poll()), Lines{"C2 00 0B F4 06 00 01 90 01 00 00"});
}

TEST_F(ConnectionXorRemapTest, TheChoiceLeavesAsF40300)
{
    OpenAndConnect(m_labConnection);

    Client::Protocol::Outgoing select = WriteServerSelect(400);
    m_labConnection.Send(std::move(select.packet), select.delivery);

    EXPECT_TRUE(SameBytes(m_socket.sent, Bytes({0xC1, 0x06, 0xF4, 0x03, 0x00, 0x00})));
}

TEST_F(ConnectionXorRemapTest, TheSkillListGetsItsByteBeforeTheDispatch)
{
    OpenAndConnect(m_labConnection);
    m_socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x11, 0x07})};

    EXPECT_EQ(Describe(m_labConnection.Poll()), Lines{"C1 06 F3 11 07 00"});
}

// Q-C09: FoxAnti.dll does not validate lengths; here an F3 11 that ends before pkt[4] is dropped with a warning.
TEST_F(ConnectionXorRemapTest, ASkillListThatDoesNotTranslateIsDropped)
{
    OpenAndConnect(m_labConnection);
    m_socket.reads = {Join({Bytes({0xC1, 0x04, 0xF3, 0x11}), Hello()})};

    EXPECT_EQ(Describe(m_labConnection.Poll()), Lines{"C1 04 00 01"});
    EXPECT_EQ(m_labConnection.State(), LinkState::Open);
    EXPECT_EQ(Warnings(), Lines{"[warning] se descarta un C1 F3 que llegó: la ofuscación no lo traduce"});
}

// 03 § La capa de FoxAnti: the lab's GameServer sends every byte with 0xAA, also after opening again.
TEST_F(ConnectionXorRemapTest, WhatArrivesFromTheGameServerIsUnmasked)
{
    const ByteVector maskedHello = Bytes({0xC1 ^ 0xAA, 0x04 ^ 0xAA, 0x00 ^ 0xAA, 0x01 ^ 0xAA});
    OpenAndConnect(m_labConnection);
    m_socket.reads = {maskedHello};
    EXPECT_EQ(Describe(m_labConnection.Poll()), Lines{"C1 04 00 01"});

    m_labConnection.Open(GameHost, GamePort, NoKeys);
    m_socket.reads = {maskedHello};
    EXPECT_EQ(Describe(m_labConnection.Poll()), (Lines{"Opened", "C1 04 00 01"}));
}

// The header gave the length, so the next packet is still found. This list says two entries and brings one.
TEST_F(ConnectionXorRemapTest, AListThatDoesNotTranslateIsDropped)
{
    OpenAndConnect(m_labConnection);
    m_socket.reads = {Join({Bytes({0xC2, 0x00, 0x0A, 0xF4, 0x02, 0x02, 0x00, 0x00, 0x00, 0xCC}), Hello()})};

    EXPECT_EQ(Describe(m_labConnection.Poll()), Lines{"C1 04 00 01"});
    EXPECT_EQ(m_labConnection.State(), LinkState::Open);
    EXPECT_EQ(Warnings(), Lines{"[warning] se descarta un C2 F4 que llegó: la ofuscación no lo traduce"});
}

// The mask comes off before decrypting, and what was decrypted still goes through BeforeDispatch.
TEST_F(ConnectionXorRemapTest, WhatArrivesEncryptedIsUnmaskedDecryptedAndTranslated)
{
    OpenTheGameServer(m_labConnection);
    PacketSerials server;
    ByteVector masked =
        Join({FromTheGameServer(Hello(), server), FromTheGameServer(Bytes({0xC1, 0x05, 0xF3, 0x11, 0x07}), server)});
    for (std::byte& value : masked)
    {
        value ^= LabMask;
    }
    m_socket.reads = {masked};

    const auto events = m_labConnection.Poll();
    ASSERT_EQ(Describe(events), (Lines{"C1 04 00 01", "C1 06 F3 11 07 00"}));
    EXPECT_TRUE(std::get<Packet>(events[0]).WasDecrypted());
    EXPECT_TRUE(std::get<Packet>(events[1]).WasDecrypted());
    EXPECT_EQ(Warnings(), Lines{});
}

// 03 § La capa de FoxAnti: the opcode 0x10 is encrypted as 0x1D, and the GameServer reads it so.
TEST_F(ConnectionXorRemapTest, WhatIsEncryptedIsRemapped)
{
    OpenTheGameServer(m_labConnection);

    m_labConnection.Send(PacketOf(Bytes({0xC1, 0x05, 0x10, 0x11, 0x22})), Delivery::Encrypted);

    PacketSerials server;
    const GameServerReceived received = AsTheGameServerReceives(m_socket.sent, server);
    ASSERT_EQ(received.packets.size(), 1u);
    EXPECT_EQ(received.packets[0].Opcode(), std::optional<std::uint8_t>{std::uint8_t{0x1D}});
}

// encode_outbound remaps a clear D3 to A3 and leaves the C3/C4 as they are.
TEST_F(ConnectionXorRemapTest, AnEncryptedD3IsNotRemapped)
{
    OpenTheGameServer(m_labConnection);

    m_labConnection.Send(PacketOf(Bytes({0xC1, 0x05, 0xD3, 0x11, 0x22})), Delivery::Encrypted);

    PacketSerials server;
    const GameServerReceived received = AsTheGameServerReceives(m_socket.sent, server);
    ASSERT_EQ(received.packets.size(), 1u);
    EXPECT_EQ(received.packets[0].Opcode(), std::optional<std::uint8_t>{std::uint8_t{0xD3}});
}

// The mask is decided per packet: a clear one and a masked one can end as the same bytes with different origins.
TEST_F(ConnectionXorRemapTest, MixedOriginsSurviveEverySplit)
{
    const ByteVector c2 = Bytes({0xC2, 0x00, 0x06, 0xF3, 0x03, 0x42});
    const ByteVector skillList = Bytes({0xC1, 0x08, 0xF3, 0x11, 0xFE, 0x04, 0x16, 0x68});
    PacketSerials server;
    ByteVector c3 = FromTheGameServer(Hello(), server);
    ByteVector c4 = FromTheGameServer(c2, server);
    ByteVector encryptedSkills = FromTheGameServer(skillList, server);
    ByteVector maskedC1 = Hello();
    for (ByteVector* part : {&c3, &c4, &maskedC1, &encryptedSkills})
    {
        for (std::byte& value : *part)
        {
            value ^= LabMask;
        }
    }
    // Both lists carry the same data; only the one that arrived encrypted keeps WasDecrypted(), at every split.
    const ByteVector stream = Join({Hello(), c3, maskedC1, c2, c4, skillList, encryptedSkills});
    const Lines expected{"C1 04 00 01",
                         "C1 04 00 01",
                         "C1 04 00 01",
                         "C2 00 06 F3 03 42",
                         "C2 00 06 F3 03 42",
                         "C1 09 F3 11 FE 00 04 16 68",
                         "C1 09 F3 11 FE 00 04 16 68"};
    const std::vector<bool> origins{false, true, false, false, true, false, true};
    for (std::size_t split = 0; split <= stream.size(); ++split)
    {
        SCOPED_TRACE(split);
        OpenTheGameServer(m_labConnection);
        m_socket.reads = {ByteVector(stream.begin(), stream.begin() + static_cast<std::ptrdiff_t>(split))};
        std::vector<Event> events = m_labConnection.Poll();
        EXPECT_TRUE(m_labConnection.Poll().empty());
        m_socket.reads = {ByteVector(stream.begin() + static_cast<std::ptrdiff_t>(split), stream.end())};
        for (Event& event : m_labConnection.Poll())
        {
            events.push_back(std::move(event));
        }
        ASSERT_EQ(Describe(events), expected);
        Lines handledBytes;
        std::vector<bool> handledOrigins;
        Client::Protocol::Dispatcher dispatcher(m_log);
        const auto record = [&](const Packet& packet)
        {
            handledBytes.push_back(HexOf(packet.Bytes()));
            handledOrigins.push_back(packet.WasDecrypted());
        };
        ASSERT_TRUE(dispatcher.On(0x00, record));
        ASSERT_TRUE(dispatcher.On(0xF3, 0x03, record));
        ASSERT_TRUE(dispatcher.On(0xF3, 0x11, record));
        for (const Event& event : events)
        {
            EXPECT_EQ(dispatcher.Dispatch(std::get<Packet>(event)), Client::Protocol::DispatchResult::Handled);
        }
        EXPECT_EQ(handledBytes, expected);
        EXPECT_EQ(handledOrigins, origins);
        EXPECT_EQ(Warnings(), Lines{});
    }
}

TEST_F(ConnectionXorRemapTest, AFailedDecryptionDoesNotContaminateTheNextClearPacket)
{
    ExpectAFailedDecryptionDoesNotContaminateTheNextClearPacket(m_xorRemap, LabMask);
}
} // namespace
