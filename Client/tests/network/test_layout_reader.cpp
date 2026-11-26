#include "Network/LayoutReader.h"

#include "Network/Field.h"
#include "Network/Packet.h"
#include "Network/PacketCode.h"
#include "Network/PacketLayout.h"
#include "Network/ProtocolError.h"
#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/NetworkPrinters.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>

namespace
{
using Client::Network::Field;
using Client::Network::LayoutReader;
using Client::Network::Opcode;
using Client::Network::Packet;
using Client::Network::PacketLayout;
using Client::Network::PacketType;
using Client::Network::ProtocolError;
using Client::Network::ReadPacket;
using Client::Network::Subcode;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;

// The GameServer's greeting, F1 00: the result, the player's index big-endian and the version.
constexpr PacketLayout Greeting{{PacketType::C1, Opcode{0xF1}, Subcode{0x00}}, 12, 0x4c1e20};
using Result = Field<std::uint8_t, 4>;
using PlayerIndex = Field<std::uint16_t, 5, std::endian::big>;
using Version = Field<std::array<std::byte, 5>, 7>;

// The main check's key, 03: no subcode, the key little-endian after a padding byte.
constexpr PacketLayout MainCheckKey{{PacketType::C1, Opcode{0x03}, std::nullopt}, 6, 0x4e16c0};
using Key = Field<std::uint16_t, 4>;

// The server list, F4 06, a C2: the count big-endian right after the subcode.
constexpr PacketLayout ServerList{{PacketType::C2, Opcode{0xF4}, Subcode{0x06}}, 7, 0x4c1860};
using Count = Field<std::uint16_t, 5, std::endian::big>;

Packet PacketOf(std::initializer_list<int> values)
{
    return Packet::FromBytes(Bytes(values)).Value();
}

Packet GreetingPacket()
{
    return PacketOf({0xC1, 0x0C, 0xF1, 0x00, 0x01, 0x12, 0x34, '1', '0', '2', '0', '9'});
}

TEST(LayoutReaderTest, ReadsEachFieldAtItsOffsetFromTheFirstByte)
{
    const Packet packet = GreetingPacket();
    auto reader = LayoutReader::Open(packet, Greeting);
    ASSERT_TRUE(reader.HasValue());
    EXPECT_EQ(reader.Value().Read<Result>(), 1U);
    EXPECT_EQ(reader.Value().Read<PlayerIndex>(), 0x1234U);
    EXPECT_EQ(reader.Value().Read<Version>(),
              (std::array{std::byte{'1'}, std::byte{'0'}, std::byte{'2'}, std::byte{'0'}, std::byte{'9'}}));
    EXPECT_EQ(reader.Value().Size(), 12U);
    EXPECT_EQ(reader.Value().Finish(7).Value(), 7);
}

TEST(LayoutReaderTest, ReadsLittleEndianUnlessTheFieldSaysOtherwise)
{
    const Packet packet = GreetingPacket();
    auto reader = LayoutReader::Open(packet, Greeting);
    ASSERT_TRUE(reader.HasValue());
    EXPECT_EQ((reader.Value().Read<Field<std::uint16_t, 5>>()), 0x3412U);
    EXPECT_EQ((reader.Value().Read<Field<std::uint32_t, 4>>()), 0x31341201U);
    EXPECT_EQ((reader.Value().Read<Field<std::uint32_t, 4, std::endian::big>>()), 0x01123431U);
}

TEST(LayoutReaderTest, RejectsAnotherTypeOpcodeOrSubcode)
{
    const Packet c2 = PacketOf({0xC2, 0x00, 0x0D, 0xF1, 0x00, 0x01, 0x12, 0x34, '1', '0', '2', '0', '9'});
    const Packet otherOpcode = PacketOf({0xC1, 0x0C, 0xF2, 0x00, 0x01, 0x12, 0x34, '1', '0', '2', '0', '9'});
    const Packet otherSubcode = PacketOf({0xC1, 0x0C, 0xF1, 0x01, 0x01, 0x12, 0x34, '1', '0', '2', '0', '9'});
    const Packet encrypted = PacketOf({0xC3, 0x0C, 0xF1, 0x00, 0x01, 0x12, 0x34, '1', '0', '2', '0', '9'});
    EXPECT_EQ(ErrorOf(LayoutReader::Open(c2, Greeting)), ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(LayoutReader::Open(otherOpcode, Greeting)), ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(LayoutReader::Open(otherSubcode, Greeting)), ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(LayoutReader::Open(encrypted, Greeting)), ProtocolError::WrongPacket);
}

TEST(LayoutReaderTest, IsTooShortWhenThePacketEndsBeforeItsSubcode)
{
    const Packet packet = PacketOf({0xC1, 0x03, 0xF1});
    EXPECT_EQ(ErrorOf(LayoutReader::Open(packet, Greeting)), ProtocolError::TooShort);
}

TEST(LayoutReaderTest, ChecksTheSubcodeBeforeTheMinimumSize)
{
    const Packet packet = PacketOf({0xC1, 0x04, 0xF1, 0x01});
    EXPECT_EQ(ErrorOf(LayoutReader::Open(packet, Greeting)), ProtocolError::WrongPacket);
}

TEST(LayoutReaderTest, IsTooShortOneByteBelowTheMinimumSize)
{
    const Packet shorter = PacketOf({0xC1, 0x0B, 0xF1, 0x00, 0x01, 0x12, 0x34, '1', '0', '2', '0'});
    EXPECT_EQ(ErrorOf(LayoutReader::Open(shorter, Greeting)), ProtocolError::TooShort);
    const Packet exact = GreetingPacket();
    EXPECT_TRUE(LayoutReader::Open(exact, Greeting).HasValue());
}

TEST(LayoutReaderTest, KeepsTheErrorOfAReadPastTheEnd)
{
    constexpr PacketLayout ResultOnly{Greeting.code, 5, Greeting.reader};
    const Packet packet = PacketOf({0xC1, 0x05, 0xF1, 0x00, 0x01});
    auto reader = LayoutReader::Open(packet, ResultOnly);
    ASSERT_TRUE(reader.HasValue());
    EXPECT_EQ(reader.Value().Read<PlayerIndex>(), 0U);
    EXPECT_EQ(reader.Value().Read<Version>(), (std::array<std::byte, 5>{}));
    EXPECT_EQ(reader.Value().Read<Result>(), 1U);
    EXPECT_EQ(ErrorOf(reader.Value().Finish(7)), ProtocolError::TooShort);
}

TEST(LayoutReaderTest, ReadsAPacketWithoutSubcodeOnItsOpcodeAlone)
{
    const Packet packet = PacketOf({0xC1, 0x06, 0x03, 0x5A, 0x79, 0xB4});
    auto reader = LayoutReader::Open(packet, MainCheckKey);
    ASSERT_TRUE(reader.HasValue());
    EXPECT_EQ(reader.Value().Read<Key>(), 0xB479U);
    const Packet shorter = PacketOf({0xC1, 0x05, 0x03, 0x00, 0x79});
    EXPECT_EQ(ErrorOf(LayoutReader::Open(shorter, MainCheckKey)), ProtocolError::TooShort);
    const Packet otherOpcode = PacketOf({0xC1, 0x06, 0x04, 0x00, 0x79, 0xB4});
    EXPECT_EQ(ErrorOf(LayoutReader::Open(otherOpcode, MainCheckKey)), ProtocolError::WrongPacket);
}

TEST(LayoutReaderTest, FindsTheSubcodeOfAC2AfterItsThreeByteHeader)
{
    const Packet packet = PacketOf({0xC2, 0x00, 0x07, 0xF4, 0x06, 0x00, 0x02});
    auto reader = LayoutReader::Open(packet, ServerList);
    ASSERT_TRUE(reader.HasValue());
    EXPECT_EQ(reader.Value().Read<Count>(), 2U);
    const Packet otherSubcode = PacketOf({0xC2, 0x00, 0x07, 0xF4, 0x03, 0x00, 0x02});
    EXPECT_EQ(ErrorOf(LayoutReader::Open(otherSubcode, ServerList)), ProtocolError::WrongPacket);
}

TEST(LayoutReaderTest, ViewsBytesInsideThePacket)
{
    const Packet packet = GreetingPacket();
    auto reader = LayoutReader::Open(packet, Greeting);
    ASSERT_TRUE(reader.HasValue());
    const std::span<const std::byte> version = reader.Value().Bytes(7, 5);
    ASSERT_EQ(version.size(), 5U);
    EXPECT_EQ(version.data(), packet.Bytes().data() + 7);
    EXPECT_TRUE(reader.Value().Bytes(12, 0).empty());
    EXPECT_TRUE(reader.Value().Finish(7).HasValue());
}

TEST(LayoutReaderTest, ViewsNothingPastTheEnd)
{
    const Packet packet = GreetingPacket();
    auto past = LayoutReader::Open(packet, Greeting);
    ASSERT_TRUE(past.HasValue());
    EXPECT_TRUE(past.Value().Bytes(10, 3).empty());
    EXPECT_EQ(ErrorOf(past.Value().Finish(7)), ProtocolError::TooShort);

    auto huge = LayoutReader::Open(packet, Greeting);
    ASSERT_TRUE(huge.HasValue());
    EXPECT_TRUE(huge.Value().Bytes(4, std::numeric_limits<std::size_t>::max()).empty());
    EXPECT_EQ(ErrorOf(huge.Value().Finish(7)), ProtocolError::TooShort);
}

TEST(ReadPacketTest, GivesWhatTheReadReturns)
{
    const Packet packet = GreetingPacket();
    const auto index = ReadPacket(packet, Greeting, [](LayoutReader& reader) { return reader.Read<PlayerIndex>(); });
    EXPECT_EQ(index.Value(), 0x1234U);
}

TEST(ReadPacketTest, DoesNotReadAnotherPacket)
{
    const Packet packet = PacketOf({0xC1, 0x0C, 0xF2, 0x00, 0x01, 0x12, 0x34, '1', '0', '2', '0', '9'});
    int reads = 0;
    const auto index = ReadPacket(packet, Greeting,
                                  [&reads](LayoutReader& reader)
                                  {
                                      ++reads;
                                      return reader.Read<PlayerIndex>();
                                  });
    EXPECT_EQ(ErrorOf(index), ProtocolError::WrongPacket);
    EXPECT_EQ(reads, 0);
}

TEST(ReadPacketTest, GivesTheErrorOfAReadPastTheEnd)
{
    constexpr PacketLayout ResultOnly{Greeting.code, 5, Greeting.reader};
    const Packet packet = PacketOf({0xC1, 0x05, 0xF1, 0x00, 0x01});
    const auto index = ReadPacket(packet, ResultOnly, [](LayoutReader& reader) { return reader.Read<PlayerIndex>(); });
    EXPECT_EQ(ErrorOf(index), ProtocolError::TooShort);
}
} // namespace
