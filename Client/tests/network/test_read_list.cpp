#include "Network/ReadList.h"

#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketCode.h"
#include "Network/PacketLayout.h"
#include "Network/ProtocolError.h"
#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/NetworkPrinters.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <initializer_list>
#include <limits>
#include <span>
#include <vector>

namespace
{
using Client::Network::LayoutReader;
using Client::Network::Opcode;
using Client::Network::Packet;
using Client::Network::PacketLayout;
using Client::Network::PacketType;
using Client::Network::ProtocolError;
using Client::Network::ReadList;
using Client::Network::Subcode;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;

// The server list, F4 06: records of four bytes from pkt[7].
constexpr PacketLayout ServerList{{PacketType::C2, Opcode{0xF4}, Subcode{0x06}}, 7, 0x4c1860};
constexpr std::size_t First = 7;
constexpr std::size_t RecordSize = 4;

Packet PacketOf(std::initializer_list<int> values)
{
    return Packet::FromBytes(Bytes(values)).Value();
}

// Two records and one byte after them.
Packet TwoServers()
{
    return PacketOf({0xC2, 0x00, 0x10, 0xF4, 0x06, 0x00, 0x02, 0x90, 0x01, 0x05, 0x00, 0x91, 0x01, 0x07, 0x00, 0xEE});
}

struct Calls
{
    int count = 0;
};

auto FirstAndThirdByte(Calls& calls)
{
    return [&calls](std::span<const std::byte, RecordSize> record)
    {
        ++calls.count;
        return std::to_integer<int>(record[0]) + (std::to_integer<int>(record[2]) << 8);
    };
}

TEST(ReadListTest, ReadsEachRecordInOrder)
{
    const Packet packet = TwoServers();
    auto reader = LayoutReader::Open(packet, ServerList);
    ASSERT_TRUE(reader.HasValue());
    Calls calls;
    const std::vector<int> list = ReadList<RecordSize>(reader.Value(), First, 2, FirstAndThirdByte(calls));
    EXPECT_EQ(list, (std::vector<int>{0x0590, 0x0791}));
    EXPECT_EQ(calls.count, 2);
    EXPECT_TRUE(reader.Value().Finish(list).HasValue());
}

TEST(ReadListTest, IsTooShortWithoutReadingWhenTheCountDoesNotFit)
{
    const Packet packet = TwoServers();
    auto reader = LayoutReader::Open(packet, ServerList);
    ASSERT_TRUE(reader.HasValue());
    Calls calls;
    const std::vector<int> list = ReadList<RecordSize>(reader.Value(), First, 3, FirstAndThirdByte(calls));
    EXPECT_TRUE(list.empty());
    EXPECT_EQ(calls.count, 0);
    EXPECT_EQ(ErrorOf(reader.Value().Finish(list)), ProtocolError::TooShort);
}

TEST(ReadListTest, ReadsNothingForAZeroCount)
{
    const Packet packet = TwoServers();
    auto reader = LayoutReader::Open(packet, ServerList);
    ASSERT_TRUE(reader.HasValue());
    Calls calls;
    const std::vector<int> list = ReadList<RecordSize>(reader.Value(), First, 0, FirstAndThirdByte(calls));
    EXPECT_TRUE(list.empty());
    EXPECT_EQ(calls.count, 0);
    EXPECT_TRUE(reader.Value().Finish(list).HasValue());
}

TEST(ReadListTest, IsTooShortForACountWhoseSizeOverflows)
{
    const Packet packet = TwoServers();
    auto reader = LayoutReader::Open(packet, ServerList);
    ASSERT_TRUE(reader.HasValue());
    Calls calls;
    const std::vector<int> list = ReadList<RecordSize>(
        reader.Value(), First, std::numeric_limits<std::size_t>::max() / 2, FirstAndThirdByte(calls));
    EXPECT_TRUE(list.empty());
    EXPECT_EQ(calls.count, 0);
    EXPECT_EQ(ErrorOf(reader.Value().Finish(list)), ProtocolError::TooShort);
}
} // namespace
