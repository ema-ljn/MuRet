#include "Network/Packet.h"
#include "Network/PacketError.h"
#include "Network/PacketReader.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/NetworkPrinters.h"
#include "support/SameBytes.h"
#include "support/WireCapture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <ostream>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// Against the lab's wire (support/WireCapture.h, MU_CLIENT_WIRE_DIR): each read goes in as recv returned it. The
// wire carries the player's login: tests count packets by type and compare with SameBytesWithoutValues, and no
// message prints a wire byte. The GameServer's S->C is not here: every byte carries FoxAnti's 0xAA XOR, and it is cut
// in test_golden_xor_remap_obfuscation.cpp.
namespace
{
using Client::Network::PacketError;
using Client::Network::PacketReader;
using Client::Network::PacketType;
using Client::Tests::ParseWireCapture;
using Client::Tests::ReadFileBytes;
using Client::Tests::SameBytesWithoutValues;
using Client::Tests::WireConnection;
using Client::Tests::WireDirection;

using GoldenPacketReader = Client::Tests::WireDataTest;

constexpr std::string_view CaptureFile = "muproxy.log";

// The proxy's ports, in front of the lab's ConnectServer and GameServer.
constexpr std::uint16_t ConnectServerPort = 44405;
constexpr std::uint16_t GameServerPort = 55901;

constexpr std::size_t PacketTypeCount = 4;

struct TypeCounts
{
    std::array<std::size_t, PacketTypeCount> byType{};

    std::size_t& operator[](PacketType type)
    {
        return byType[static_cast<std::size_t>(type) - static_cast<std::size_t>(PacketType::C1)];
    }
    bool operator==(const TypeCounts&) const = default;

    // Without this GoogleTest prints the struct's memory.
    friend void PrintTo(const TypeCounts& counts, std::ostream* out)
    {
        *out << "C1=" << counts.byType[0] << " C2=" << counts.byType[1] << " C3=" << counts.byType[2]
             << " C4=" << counts.byType[3];
    }
};

struct Cut
{
    TypeCounts counts;
    std::size_t leftover = 0;         // bytes that arrived and did not leave in a packet
    std::optional<PacketError> error; // the first one, if any
    std::vector<std::byte> stream;    // everything that went in, in order
    std::vector<std::byte> rejoined;  // the packets that left, one after the other
    std::vector<std::size_t> sizes;
};

// One Feed per read, and every packet available after each.
Cut CutReads(const std::vector<std::span<const std::byte>>& reads)
{
    PacketReader reader;
    Cut cut;
    for (const std::span<const std::byte> read : reads)
    {
        cut.stream.insert(cut.stream.end(), read.begin(), read.end());
        reader.Feed(read);
        while (true)
        {
            auto next = reader.Next();
            if (!next.HasValue())
            {
                cut.error = next.Error();
                return cut;
            }
            if (!next.Value())
            {
                break;
            }
            const auto bytes = next.Value()->Bytes();
            cut.rejoined.insert(cut.rejoined.end(), bytes.begin(), bytes.end());
            cut.sizes.push_back(bytes.size());
            ++cut.counts[next.Value()->Type()];
        }
    }
    cut.leftover = reader.Pending();
    return cut;
}

// Each header arrives over several Feeds.
std::vector<std::span<const std::byte>> OneByteReads(std::span<const std::byte> stream)
{
    std::vector<std::span<const std::byte>> reads;
    for (std::size_t i = 0; i < stream.size(); ++i)
    {
        reads.push_back(stream.subspan(i, 1));
    }
    return reads;
}

// Each read brings the rest of one packet and the start of the next.
std::vector<std::span<const std::byte>>
ReadsEndingInsideTheNext(std::span<const std::byte> stream, const std::vector<std::size_t>& sizes, std::size_t into)
{
    std::vector<std::span<const std::byte>> reads;
    std::size_t start = 0;
    std::size_t boundary = 0;
    for (std::size_t k = 0; k + 1 < sizes.size(); ++k)
    {
        boundary += sizes[k];
        const std::size_t end = boundary + std::min(into, sizes[k + 1]);
        reads.push_back(stream.subspan(start, end - start));
        start = end;
    }
    reads.push_back(stream.subspan(start));
    return reads;
}

TypeCounts Counts(std::size_t c1, std::size_t c2, std::size_t c3, std::size_t c4)
{
    return TypeCounts{{c1, c2, c3, c4}};
}

class GoldenPacketReaderCapture : public GoldenPacketReader
{
protected:
    void SetUp() override
    {
        GoldenPacketReader::SetUp();
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
    }

    // Each cut must end without an error, without leftover bytes, and with the packets equal to the stream.
    TypeCounts CutAll(std::uint16_t port, WireDirection direction, std::size_t& cutConnections) const
    {
        TypeCounts total;
        cutConnections = 0;
        for (std::size_t index = 0; index < m_connections.size(); ++index)
        {
            const WireConnection& connection = m_connections[index];
            if (connection.port != port)
            {
                continue;
            }
            ++cutConnections;
            const Cut cut = CutReads(connection.Reads(direction));
            EXPECT_EQ(cut.error, std::nullopt) << "conexión " << index;
            EXPECT_EQ(cut.leftover, 0u) << "conexión " << index;
            EXPECT_TRUE(SameBytesWithoutValues(cut.rejoined, cut.stream)) << "conexión " << index;
            for (std::size_t type = 0; type < PacketTypeCount; ++type)
            {
                total.byType[type] += cut.counts.byType[type];
            }
        }
        return total;
    }

    // The client's sends, to both servers, and the ConnectServer's answers, which carry the C2s.
    template <typename Check> void ForEachClearDirection(Check check) const
    {
        for (std::size_t index = 0; index < m_connections.size(); ++index)
        {
            const WireConnection& connection = m_connections[index];
            {
                SCOPED_TRACE("C->S");
                check(index, connection, WireDirection::ClientToServer);
            }
            if (connection.port == ConnectServerPort)
            {
                SCOPED_TRACE("S->C");
                check(index, connection, WireDirection::ServerToClient);
            }
        }
    }

    std::vector<WireConnection> m_connections;
};

// Without printing bytes.
void ExpectTheSamePackets(const Cut& resplit, const Cut& asRead, std::size_t index)
{
    EXPECT_EQ(resplit.error, std::nullopt) << "conexión " << index;
    EXPECT_EQ(resplit.leftover, 0u) << "conexión " << index;
    EXPECT_EQ(resplit.counts, asRead.counts) << "conexión " << index;
    EXPECT_TRUE(resplit.sizes == asRead.sizes) << "conexión " << index << ": otros cortes";
    EXPECT_TRUE(SameBytesWithoutValues(resplit.rejoined, asRead.stream)) << "conexión " << index;
}

TEST_F(GoldenPacketReaderCapture, HasTheLabConnections)
{
    std::size_t connectServer = 0;
    std::size_t gameServer = 0;
    for (const WireConnection& connection : m_connections)
    {
        connectServer += connection.port == ConnectServerPort ? 1 : 0;
        gameServer += connection.port == GameServerPort ? 1 : 0;
    }

    EXPECT_EQ(m_connections.size(), 72u);
    EXPECT_EQ(connectServer, 64u);
    EXPECT_EQ(gameServer, 8u);
}

// To the ConnectServer, 80 F4 in the clear (72 F4 02 and 8 F4 03) and 64 A9 with XOR32; to the GameServer, the login
// and the rest, with the C3s not decrypted.
TEST_F(GoldenPacketReaderCapture, CutsWhatTheClientSent)
{
    std::size_t cutConnections = 0;

    EXPECT_EQ(CutAll(ConnectServerPort, WireDirection::ClientToServer, cutConnections), Counts(144, 0, 0, 0));
    EXPECT_EQ(cutConnections, 64u);
    EXPECT_EQ(CutAll(GameServerPort, WireDirection::ClientToServer, cutConnections), Counts(362, 0, 183, 0));
    EXPECT_EQ(cutConnections, 8u);
}

// The greeting and the list or a server's address.
TEST_F(GoldenPacketReaderCapture, CutsWhatTheConnectServerAnswered)
{
    std::size_t cutConnections = 0;

    EXPECT_EQ(CutAll(ConnectServerPort, WireDirection::ServerToClient, cutConnections), Counts(72, 72, 0, 0));
    EXPECT_EQ(cutConnections, 64u);
}

// The original does not fail here, because with fewer than 3 bytes it does not cut (0x4e8927) and never has two
// packets left in one pass.
TEST_F(GoldenPacketReaderCapture, OneByteAtATimeGivesTheSamePackets)
{
    ForEachClearDirection(
        [](std::size_t index, const WireConnection& connection, WireDirection direction)
        {
            const Cut asRead = CutReads(connection.Reads(direction));
            ExpectTheSamePackets(CutReads(OneByteReads(asRead.stream)), asRead, index);
        });
}

// Reads ending 1 and 2 bytes inside the next packet: what triggers Q-C03 in the original, an incomplete header behind
// a whole packet in the same read. With 2, the ConnectServer's C2s miss the header's third byte.
TEST_F(GoldenPacketReaderCapture, ReadsEndingInsideTheNextHeaderGiveTheSamePackets)
{
    for (const std::size_t into : {std::size_t{1}, std::size_t{2}})
    {
        ForEachClearDirection(
            [into](std::size_t index, const WireConnection& connection, WireDirection direction)
            {
                const Cut asRead = CutReads(connection.Reads(direction));
                const Cut resplit = CutReads(ReadsEndingInsideTheNext(asRead.stream, asRead.sizes, into));
                SCOPED_TRACE(into == 1 ? "1 byte adentro" : "2 bytes adentro");
                ExpectTheSamePackets(resplit, asRead, index);
            });
    }
}
} // namespace
