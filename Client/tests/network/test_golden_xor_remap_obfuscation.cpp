#include "App/LabXorRemapTables.h"
#include "Core/ByteReader.h"
#include "Crypto/SimpleModulus.h"
#include "Crypto/SimpleModulusKeyFile.h"
#include "Network/EncryptedPacket.h"
#include "Network/Packet.h"
#include "Network/PacketReader.h"
#include "Network/PacketSerials.h"
#include "Network/XorRemap/XorRemapObfuscation.h"
#include "Protocol/Session/ServerList.h"
#include "Network/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/KeyFiles.h"
#include "support/NetworkPrinters.h"
#include "support/SameBytes.h"
#include "support/WireCapture.h"
#include "support/WirePackets.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// XorRemapObfuscation against the lab wire (support/WireCapture.h, MU_CLIENT_WIRE_DIR): what the ConnectServer and
// the GameServer answered, cut with the 0xAA per packet, and the translations of the list, of the F3 11 and of the
// server code the client chose. CL2 plan, CL2.4, step 4.
//
// The wire carries the player's login: these tests count packets and compare with SameBytesWithoutValues. No
// message prints a byte of the wire or of what was decrypted.
namespace
{
using Client::App::LabXorRemapTables;
using Client::Core::ByteReader;
using Client::Crypto::DecryptionKey;
using Client::Crypto::LoadDecryptionKey;
using Client::Network::DecryptPacket;
using Client::Network::Packet;
using Client::Network::PacketReader;
using Client::Network::PacketSerials;
using Client::Network::PacketType;
using Client::Network::ProtocolError;
using Client::Network::XorRemap::XorRemapObfuscation;
using Client::Protocol::Session::ReadServerList;
using Client::Tests::Bytes;
using Client::Tests::CutPackets;
using Client::Tests::ErrorOf;
using Client::Tests::LoadKeyFile;
using Client::Tests::ParseWireCapture;
using Client::Tests::ReadFileBytes;
using Client::Tests::SameBytesWithoutValues;
using Client::Tests::WireConnection;
using Client::Tests::WireDirection;

using ByteVector = std::vector<std::byte>;

// Written by the lab's proxy.
constexpr std::string_view CaptureFile = "muproxy.log";

// The proxy's ports, in front of the lab's ConnectServer and GameServer.
constexpr std::uint16_t ConnectServerPort = 44405;
constexpr std::uint16_t GameServerPort = 55901;

// The lab's XOR on what the GameServer sends.
constexpr std::byte LabMask{0xAA};

// What cutting one direction of a connection with FoxAnti gave.
struct Cut
{
    std::vector<Packet> packets;
    std::size_t masked = 0;   // how many arrived with the 0xAA
    std::size_t leftover = 0; // bytes that arrived and did not come out in a packet
    bool failed = false;      // the cut stopped with an error
    bool sameAsWire = true;   // each packet, with its mask, is what went through the wire
};

// Cuts `reads` with a PacketReader with FoxAnti, one Feed per read. Each packet that comes out is compared with its
// place in the wire stream: equal if it arrived in clear, or with the 0xAA on each byte.
Cut CutWithXorRemap(const std::vector<std::span<const std::byte>>& reads)
{
    const XorRemapObfuscation xorRemap{LabXorRemapTables()};
    PacketReader reader(xorRemap);
    Cut cut;
    ByteVector stream;
    for (const std::span<const std::byte> read : reads)
    {
        stream.insert(stream.end(), read.begin(), read.end());
    }
    std::size_t offset = 0;
    for (const std::span<const std::byte> read : reads)
    {
        reader.Feed(read);
        while (true)
        {
            auto next = reader.Next();
            if (!next.HasValue())
            {
                cut.failed = true;
                return cut;
            }
            if (!next.Value())
            {
                break;
            }
            Packet packet = std::move(*std::move(next).Value());
            const std::span<const std::byte> wire = std::span<const std::byte>(stream).subspan(offset);
            const bool masked = wire.front() != packet.Bytes().front();
            ByteVector remasked(packet.Bytes().begin(), packet.Bytes().end());
            for (std::byte& value : remasked)
            {
                value ^= masked ? LabMask : std::byte{0};
            }
            cut.sameAsWire = cut.sameAsWire && wire.size() >= remasked.size() &&
                             SameBytesWithoutValues(wire.first(remasked.size()), remasked);
            cut.masked += masked ? 1 : 0;
            offset += remasked.size();
            cut.packets.push_back(std::move(packet));
        }
    }
    cut.leftover = reader.Pending();
    return cut;
}

std::vector<std::span<const std::byte>> OneByteReads(std::span<const std::byte> stream)
{
    std::vector<std::span<const std::byte>> reads;
    for (std::size_t i = 0; i < stream.size(); ++i)
    {
        reads.push_back(stream.subspan(i, 1));
    }
    return reads;
}

// A C1/C2 with this opcode and this subcode, in the byte that follows it.
bool Is(const Packet& packet, PacketType type, std::uint8_t opcode, std::uint8_t subcode)
{
    const std::span<const std::byte> bytes = packet.Bytes();
    const std::size_t subcodeIndex = type == PacketType::C1 ? 3 : 4;
    return packet.Type() == type && packet.Opcode() == Client::Network::Opcode{opcode} && bytes.size() > subcodeIndex &&
           bytes[subcodeIndex] == static_cast<std::byte>(subcode);
}

class GoldenXorRemapObfuscation : public Client::Tests::GoldenAndWireDataTest
{
protected:
    void SetUp() override
    {
        GoldenAndWireDataTest::SetUp();
        if (IsSkipped() || HasFatalFailure())
        {
            return;
        }
        const auto path = WireFile(CaptureFile);
        ASSERT_TRUE(path.has_value());
        const auto file = ReadFileBytes(*path);
        ASSERT_TRUE(file.has_value()) << "no se pudo leer " << CaptureFile;
        auto parsed = ParseWireCapture(std::string_view(file->data(), file->size()));
        ASSERT_TRUE(parsed.HasValue()) << CaptureFile << ": la línea " << parsed.Error().line << " no se entiende";
        m_connections = std::move(parsed).Value();

        // The client decrypts what the server answers with Dec2.dat.
        m_dec2 = LoadKeyFile(DataFile("Dec2.dat"), LoadDecryptionKey);
        ASSERT_TRUE(m_dec2);
    }

    std::vector<WireConnection> m_connections;
    std::optional<DecryptionKey> m_dec2;
};

// What both servers answered, over the 72 connections: 3033 packets with no extra bytes. The ConnectServer's 144
// arrive in clear (the welcome and the list or the address), and the GameServer's 2889 all with the 0xAA, the C3/C4
// too.
TEST_F(GoldenXorRemapObfuscation, CutsWhatBothServersAnswered)
{
    std::size_t connectServer = 0;
    std::size_t gameServer = 0;
    std::size_t masked = 0;
    for (std::size_t index = 0; index < m_connections.size(); ++index)
    {
        const WireConnection& connection = m_connections[index];
        const Cut cut = CutWithXorRemap(connection.Reads(WireDirection::ServerToClient));
        EXPECT_FALSE(cut.failed) << "conexión " << index;
        EXPECT_EQ(cut.leftover, 0u) << "conexión " << index;
        EXPECT_TRUE(cut.sameAsWire) << "conexión " << index;
        (connection.port == ConnectServerPort ? connectServer : gameServer) += cut.packets.size();
        masked += cut.masked;
        if (connection.port == ConnectServerPort)
        {
            EXPECT_EQ(cut.masked, 0u) << "conexión " << index;
        }
        else
        {
            EXPECT_EQ(cut.masked, cut.packets.size()) << "conexión " << index;
        }
    }

    EXPECT_EQ(connectServer, 144u);
    EXPECT_EQ(gameServer, 2889u);
    EXPECT_EQ(masked, 2889u);
}

// One byte at a time gives the same GameServer packets: each one keeps the mask its first byte chose, even if the
// rest arrive in other reads.
TEST_F(GoldenXorRemapObfuscation, OneByteAtATimeGivesTheSamePackets)
{
    for (std::size_t index = 0; index < m_connections.size(); ++index)
    {
        const WireConnection& connection = m_connections[index];
        if (connection.port != GameServerPort)
        {
            continue;
        }
        const Cut asRead = CutWithXorRemap(connection.Reads(WireDirection::ServerToClient));
        ByteVector stream;
        for (const std::span<const std::byte> read : connection.Reads(WireDirection::ServerToClient))
        {
            stream.insert(stream.end(), read.begin(), read.end());
        }
        const Cut oneByte = CutWithXorRemap(OneByteReads(stream));

        EXPECT_FALSE(oneByte.failed) << "conexión " << index;
        EXPECT_EQ(oneByte.leftover, 0u) << "conexión " << index;
        EXPECT_TRUE(oneByte.sameAsWire) << "conexión " << index;
        ASSERT_EQ(oneByte.packets.size(), asRead.packets.size()) << "conexión " << index;
        for (std::size_t k = 0; k < oneByte.packets.size(); ++k)
        {
            EXPECT_TRUE(SameBytesWithoutValues(oneByte.packets[k].Bytes(), asRead.packets[k].Bytes()))
                << "conexión " << index << ", paquete " << k;
        }
    }
}

// The ConnectServer's 72 F4 02 lists become main.exe's F4 06: one server, with the lab's code 0 as 400, the load
// copied and the fourth byte at 0.
TEST_F(GoldenXorRemapObfuscation, TheLabServerListsBecomeTheOnesMainExeReads)
{
    const XorRemapObfuscation xorRemap{LabXorRemapTables()};
    const ByteVector expectedStart = Bytes({0xC2, 0x00, 0x0B, 0xF4, 0x06, 0x00, 0x01, 0x90, 0x01});
    constexpr std::size_t ListSize = 11;      // main.exe's, with its only entry
    constexpr std::size_t LabLoadByte = 8;    // the load in the lab's list
    constexpr std::size_t ListLoadByte = 9;   // and in main.exe's, one byte further on
    constexpr std::size_t LastEntryByte = 10; // the entry's fourth byte, which comes out as 0
    std::size_t lists = 0;
    for (std::size_t index = 0; index < m_connections.size(); ++index)
    {
        const WireConnection& connection = m_connections[index];
        if (connection.port != ConnectServerPort)
        {
            continue;
        }
        for (Packet& packet : CutWithXorRemap(connection.Reads(WireDirection::ServerToClient)).packets)
        {
            if (!Is(packet, PacketType::C2, 0xF4, 0x02))
            {
                continue;
            }
            ++lists;
            ASSERT_GT(packet.Bytes().size(), LabLoadByte) << "conexión " << index;
            const std::byte labLoad = packet.Bytes()[LabLoadByte];
            const auto list = xorRemap.AfterCut(std::move(packet));
            ASSERT_TRUE(list.HasValue()) << "conexión " << index;
            const std::span<const std::byte> bytes = list.Value().Bytes();
            ASSERT_EQ(bytes.size(), ListSize) << "conexión " << index;
            EXPECT_TRUE(SameBytesWithoutValues(bytes.first(expectedStart.size()), expectedStart))
                << "conexión " << index;
            EXPECT_TRUE(bytes[ListLoadByte] == labLoad) << "conexión " << index;
            EXPECT_TRUE(bytes[LastEntryByte] == std::byte{0}) << "conexión " << index;
        }
    }

    EXPECT_EQ(lists, 72u);
}

// The ConnectServer's 72 lists arrive as F4 02, which ReadServerList does not read; with XorRemapObfuscation's
// AfterCut (CL2.4) they become F4 06 and are all read, with the entries their count says.
TEST_F(GoldenXorRemapObfuscation, ReadServerListReadsTheListsOnlyAfterTheLayer)
{
    const XorRemapObfuscation xorRemap{LabXorRemapTables()};
    constexpr std::size_t ListCountOffset = 5; // the server count in main.exe's list
    std::size_t lists = 0;
    std::size_t rejectedAsTheyArrive = 0;
    std::size_t readWithTheirCount = 0;
    for (std::size_t index = 0; index < m_connections.size(); ++index)
    {
        if (m_connections[index].port != ConnectServerPort)
        {
            continue;
        }
        for (Packet& packet : CutPackets(m_connections[index].Reads(WireDirection::ServerToClient), index))
        {
            if (!Is(packet, PacketType::C2, 0xF4, 0x02))
            {
                continue;
            }
            ++lists;
            if (ErrorOf(ReadServerList(packet)) == ProtocolError::WrongPacket)
            {
                ++rejectedAsTheyArrive;
            }
            const auto translated = xorRemap.AfterCut(std::move(packet));
            ASSERT_TRUE(translated.HasValue()) << "conexión " << index;
            const auto entries = ReadServerList(translated.Value());
            ASSERT_TRUE(entries.HasValue()) << "conexión " << index;
            ByteReader header(translated.Value().Bytes().subspan(ListCountOffset));
            const std::optional<std::uint16_t> count = header.ReadU16Be();
            if (count && entries.Value().size() == *count)
            {
                ++readWithTheirCount;
            }
        }
    }

    EXPECT_EQ(lists, 72u);
    EXPECT_EQ(rejectedAsTheyArrive, 72u);
    EXPECT_EQ(readWithTheirCount, 72u);
}

// Everything the GameServer answered goes through the three receive stages without an InvalidPacket: the cut with
// the 0xAA, AfterCut, the decryption of the C3/C4 with Dec2.dat, and BeforeDispatch. The 7 F3 11, all of 8 bytes,
// come out one byte longer with the 00 at pkt[5].
TEST_F(GoldenXorRemapObfuscation, EverythingTheGameServerAnsweredReachesTheDispatcher)
{
    const XorRemapObfuscation xorRemap{LabXorRemapTables()};
    constexpr std::size_t LabSkillListSize = 8; // the wire's F3 11
    constexpr std::size_t InsertedByte = 5;     // where reset_queue puts the 00
    std::size_t dispatched = 0;
    std::size_t skillLists = 0;
    for (std::size_t index = 0; index < m_connections.size(); ++index)
    {
        const WireConnection& connection = m_connections[index];
        if (connection.port != GameServerPort)
        {
            continue;
        }
        PacketSerials serials;
        for (Packet& cut : CutWithXorRemap(connection.Reads(WireDirection::ServerToClient)).packets)
        {
            auto afterCut = xorRemap.AfterCut(std::move(cut));
            ASSERT_TRUE(afterCut.HasValue()) << "conexión " << index;
            Packet packet = std::move(afterCut).Value();
            if (packet.Type() == PacketType::C3 || packet.Type() == PacketType::C4)
            {
                auto decrypted = DecryptPacket(packet, *m_dec2, serials);
                ASSERT_TRUE(decrypted.HasValue()) << "conexión " << index;
                packet = std::move(decrypted).Value().packet;
            }
            const bool skillList = Is(packet, PacketType::C1, 0xF3, 0x11);
            const std::size_t sizeBefore = packet.Bytes().size();
            const ByteVector before(packet.Bytes().begin(), packet.Bytes().end());

            const auto toDispatch = xorRemap.BeforeDispatch(std::move(packet));

            ASSERT_TRUE(toDispatch.HasValue()) << "conexión " << index;
            ++dispatched;
            if (!skillList)
            {
                continue;
            }
            ++skillLists;
            const std::span<const std::byte> after = toDispatch.Value().Bytes();
            EXPECT_EQ(sizeBefore, LabSkillListSize) << "conexión " << index;
            ASSERT_EQ(after.size(), sizeBefore + 1) << "conexión " << index;
            EXPECT_TRUE(after[InsertedByte] == std::byte{0}) << "conexión " << index;
            EXPECT_TRUE(SameBytesWithoutValues(after.subspan(InsertedByte + 1),
                                               std::span<const std::byte>(before).subspan(InsertedByte)))
                << "conexión " << index;
        }
    }

    EXPECT_EQ(dispatched, 2889u);
    EXPECT_EQ(skillLists, 7u);
}

// The client's 8 F4 03 to the ConnectServer are what BeforeSend gives with main.exe's code 400: the 0 the lab
// expects.
TEST_F(GoldenXorRemapObfuscation, TheChosenServerIsWhatTheOriginalClientSent)
{
    const XorRemapObfuscation xorRemap{LabXorRemapTables()};
    const Packet chosen = xorRemap.BeforeSend(Packet::FromBytes(Bytes({0xC1, 0x06, 0xF4, 0x03, 0x90, 0x01})).Value());
    std::size_t selects = 0;
    for (std::size_t index = 0; index < m_connections.size(); ++index)
    {
        const WireConnection& connection = m_connections[index];
        if (connection.port != ConnectServerPort)
        {
            continue;
        }
        for (const Packet& packet : CutWithXorRemap(connection.Reads(WireDirection::ClientToServer)).packets)
        {
            if (!Is(packet, PacketType::C1, 0xF4, 0x03))
            {
                continue;
            }
            ++selects;
            EXPECT_TRUE(SameBytesWithoutValues(packet.Bytes(), chosen.Bytes())) << "conexión " << index;
        }
    }

    EXPECT_EQ(selects, 8u);
}
} // namespace
