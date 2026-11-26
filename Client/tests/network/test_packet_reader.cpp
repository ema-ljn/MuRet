#include "Network/PacketReader.h"

#include "Network/Packet.h"
#include "Network/PacketError.h"
#include "support/Bytes.h"
#include "support/FixedRandom.h"
#include "support/NetworkPrinters.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <ios>
#include <iterator>
#include <optional>
#include <random>
#include <span>
#include <utility>
#include <vector>

// The CL2 plan's vector table (CL2.1, step 4): red_reference.py --selftest's cases outside encryption, and the quirks
// it fixes.
namespace
{
using Client::Network::MaxPacketSize;
using Client::Network::Packet;
using Client::Network::PacketError;
using Client::Network::PacketReader;
using Client::Network::PacketType;
using Client::Tests::Below;
using Client::Tests::Bytes;
using Client::Tests::FixedSeed;
using Client::Tests::SameBytes;

using ByteVector = std::vector<std::byte>;

ByteVector Join(const ByteVector& first, const ByteVector& second)
{
    ByteVector joined = first;
    joined.insert(joined.end(), second.begin(), second.end());
    return joined;
}

struct Drained
{
    std::vector<ByteVector> packets;
    std::optional<PacketError> error;
};

Drained Drain(PacketReader& reader)
{
    Drained drained;
    while (true)
    {
        auto next = reader.Next();
        if (!next.HasValue())
        {
            drained.error = next.Error();
            return drained;
        }
        if (!next.Value().has_value())
        {
            return drained;
        }
        const std::span<const std::byte> bytes = next.Value()->Bytes();
        drained.packets.emplace_back(bytes.begin(), bytes.end());
    }
}

// red_reference.py's case 1.
const ByteVector FirstC1 = Bytes({0xC1, 0x05, 0x10, 0x01, 0x02});
const ByteVector SecondC2 = Bytes({0xC2, 0x00, 0x06, 0x20, 0x03, 0x04});

ByteVector PacketOfSize(PacketType type, std::size_t size, std::byte fill)
{
    ByteVector bytes(size, fill);
    bytes[0] = static_cast<std::byte>(type);
    if (type == PacketType::C1 || type == PacketType::C3)
    {
        bytes[1] = static_cast<std::byte>(size);
        return bytes;
    }
    bytes[1] = static_cast<std::byte>(size >> 8);
    bytes[2] = static_cast<std::byte>(size & 0xFF);
    return bytes;
}

TEST(PacketReaderTest, CutsAC1)
{
    PacketReader reader;
    reader.Feed(Bytes({0xC1, 0x04, 0x20, 0xAA}));

    const Drained drained = Drain(reader);

    ASSERT_EQ(drained.packets.size(), 1u);
    EXPECT_TRUE(SameBytes(drained.packets[0], Bytes({0xC1, 0x04, 0x20, 0xAA})));
    EXPECT_FALSE(drained.error.has_value());
    EXPECT_EQ(reader.Pending(), 0u);
}

// C2 01 2C is 300 bytes: big-endian, with the high byte non-zero.
TEST(PacketReaderTest, CutsAC2Of300Bytes)
{
    const ByteVector packet = PacketOfSize(PacketType::C2, 300, std::byte{0x5A});
    PacketReader reader;
    reader.Feed(packet);

    const Drained drained = Drain(reader);

    ASSERT_EQ(drained.packets.size(), 1u);
    EXPECT_TRUE(SameBytes(drained.packets[0], packet));
}

TEST(PacketReaderTest, CutsC3AndC4WithoutDecrypting)
{
    const ByteVector c3 = PacketOfSize(PacketType::C3, 13, std::byte{0x77});
    const ByteVector c4 = PacketOfSize(PacketType::C4, 300, std::byte{0x88});
    PacketReader reader;
    reader.Feed(Join(c3, c4));

    const Drained drained = Drain(reader);

    ASSERT_EQ(drained.packets.size(), 2u);
    EXPECT_TRUE(SameBytes(drained.packets[0], c3));
    EXPECT_TRUE(SameBytes(drained.packets[1], c4));
}

// red_reference.py's case 1: the C1 and the C2's first 3 bytes in one read, the rest in another.
TEST(PacketReaderTest, KeepsThePartialRestForTheNextFeed)
{
    PacketReader reader;
    const ByteVector stream = Join(FirstC1, SecondC2);
    const std::span<const std::byte> bytes(stream);
    const std::size_t firstRead = FirstC1.size() + 3;

    reader.Feed(bytes.first(firstRead));
    const Drained first = Drain(reader);
    ASSERT_EQ(first.packets.size(), 1u);
    EXPECT_TRUE(SameBytes(first.packets[0], FirstC1));
    EXPECT_EQ(reader.Pending(), 3u);

    reader.Feed(bytes.subspan(firstRead));
    const Drained second = Drain(reader);
    ASSERT_EQ(second.packets.size(), 1u);
    EXPECT_TRUE(SameBytes(second.packets[0], SecondC2));
    EXPECT_EQ(reader.Pending(), 0u);
}

// Q-C03: split after C2 or C2 00, behind a whole C1 in the same read, the original builds the size from stale bytes
// (with C2 00 and a stale 0 it yields C1 05 06 20 03, CL2.0's boss). The reader waits and gives the same two packets.
TEST(PacketReaderTest, WaitsOnAnIncompleteC2Header)
{
    for (const std::size_t headerBytes : {std::size_t{1}, std::size_t{2}})
    {
        PacketReader reader;
        const ByteVector stream = Join(FirstC1, SecondC2);
        const std::span<const std::byte> bytes(stream);
        const std::size_t firstRead = FirstC1.size() + headerBytes;

        reader.Feed(bytes.first(firstRead));
        const Drained first = Drain(reader);
        reader.Feed(bytes.subspan(firstRead));
        const Drained second = Drain(reader);

        ASSERT_EQ(first.packets.size(), 1u) << "partido después de " << headerBytes << " bytes del C2";
        EXPECT_TRUE(SameBytes(first.packets[0], FirstC1));
        ASSERT_EQ(second.packets.size(), 1u) << "partido después de " << headerBytes << " bytes del C2";
        EXPECT_TRUE(SameBytes(second.packets[0], SecondC2));
    }
}

// Q-C03 with a C1's or C3's first byte behind a whole C1 in the same read: the original reads the size from a stale
// byte.
TEST(PacketReaderTest, WaitsOnATypeByteBehindAPacket)
{
    for (const int type : {0xC1, 0xC3})
    {
        PacketReader reader;

        reader.Feed(Join(FirstC1, Bytes({type})));
        const Drained first = Drain(reader);
        reader.Feed(Bytes({0x04, 0x20, 0xAA}));
        const Drained second = Drain(reader);

        ASSERT_EQ(first.packets.size(), 1u) << "tipo 0x" << std::hex << type;
        EXPECT_TRUE(SameBytes(first.packets[0], FirstC1));
        ASSERT_EQ(second.packets.size(), 1u) << "tipo 0x" << std::hex << type;
        EXPECT_TRUE(SameBytes(second.packets[0], Bytes({type, 0x04, 0x20, 0xAA})));
        EXPECT_EQ(reader.Pending(), 0u);
    }
}

// Here the original waits too: with fewer than 3 bytes it does not cut (0x4e8927, cmp eax,0x3).
TEST(PacketReaderTest, WaitsOnALoneTypeByte)
{
    PacketReader reader;

    reader.Feed(Bytes({0xC1}));
    const Drained first = Drain(reader);
    reader.Feed(Bytes({0x04, 0x20, 0xAA}));
    const Drained second = Drain(reader);

    EXPECT_TRUE(first.packets.empty());
    EXPECT_FALSE(first.error.has_value());
    ASSERT_EQ(second.packets.size(), 1u);
    EXPECT_TRUE(SameBytes(second.packets[0], Bytes({0xC1, 0x04, 0x20, 0xAA})));
}

// red_reference.py's case 2.
TEST(PacketReaderTest, WaitsForTheBody)
{
    PacketReader reader;

    reader.Feed(Bytes({0xC1, 0x03}));
    const Drained first = Drain(reader);
    EXPECT_TRUE(first.packets.empty());
    EXPECT_FALSE(first.error.has_value());

    reader.Feed(Bytes({0x00}));
    const Drained second = Drain(reader);
    ASSERT_EQ(second.packets.size(), 1u);
    EXPECT_TRUE(SameBytes(second.packets[0], Bytes({0xC1, 0x03, 0x00})));
}

// The wire had reads with 2 to 7 packets together (03, "Cadena de recepción").
TEST(PacketReaderTest, CutsSeveralPacketsInOneFeed)
{
    ByteVector stream;
    for (int k = 0; k < 7; ++k)
    {
        stream = Join(stream, Bytes({0xC1, 0x04, 0x20, k}));
    }
    PacketReader reader;
    reader.Feed(stream);

    const Drained drained = Drain(reader);

    ASSERT_EQ(drained.packets.size(), 7u);
    EXPECT_TRUE(SameBytes(drained.packets[6], Bytes({0xC1, 0x04, 0x20, 6})));
}

// red_reference.py's case 3 and Q-C38: the good packet behind does not come out (the original drops both and goes on
// with what arrives).
TEST(PacketReaderTest, StopsOnAnInvalidType)
{
    PacketReader reader;
    reader.Feed(Join(Bytes({0x00, 0x05, 0x01, 0x02, 0x03}), FirstC1));

    const Drained drained = Drain(reader);

    EXPECT_TRUE(drained.packets.empty());
    EXPECT_EQ(drained.error, PacketError::InvalidType);
}

// red_reference.py's case 4 and Q-C39: an error as soon as the header arrives. The original, with 0, never advances;
// with 1 or 2 in a C1/C3, or 1 to 3 in a C2/C4, it yields a packet without an opcode.
TEST(PacketReaderTest, StopsOnASizeSmallerThanHeaderAndOpcode)
{
    for (const ByteVector& header :
         {Bytes({0xC1, 0x00}), Bytes({0xC1, 0x01}), Bytes({0xC1, 0x02}), Bytes({0xC3, 0x02}), Bytes({0xC2, 0x00, 0x00}),
          Bytes({0xC2, 0x00, 0x01}), Bytes({0xC2, 0x00, 0x03}), Bytes({0xC4, 0x00, 0x03})})
    {
        PacketReader reader;
        reader.Feed(header);

        const Drained drained = Drain(reader);

        EXPECT_TRUE(drained.packets.empty());
        EXPECT_EQ(drained.error, PacketError::SizeTooSmall) << "cabecera de " << header.size() << " bytes";
    }
}

// Q-C40: C2 20 00 is MaxPacketSize bytes and is awaited; C2 20 01 is rejected with the header alone.
TEST(PacketReaderTest, RejectsMoreThanMaxPacketSizeWithTheHeaderAlone)
{
    PacketReader largest;
    largest.Feed(Bytes({0xC2, 0x20, 0x00}));
    const Drained waiting = Drain(largest);
    EXPECT_TRUE(waiting.packets.empty());
    EXPECT_FALSE(waiting.error.has_value());

    const ByteVector packet = PacketOfSize(PacketType::C2, MaxPacketSize, std::byte{0x33});
    largest.Feed(std::span<const std::byte>(packet).subspan(3));
    ASSERT_EQ(Drain(largest).packets.size(), 1u);

    PacketReader tooLarge;
    tooLarge.Feed(Bytes({0xC2, 0x20, 0x01}));
    EXPECT_EQ(Drain(tooLarge).error, PacketError::TooLarge);
}

// Q-C38: after an error Next() repeats it forever, even if a good packet arrives, and the reader keeps nothing. With
// each of the three framing errors.
TEST(PacketReaderTest, KeepsTheErrorForever)
{
    struct Case
    {
        ByteVector header;
        PacketError error;
    };
    const Case cases[] = {
        {Bytes({0xC5}), PacketError::InvalidType},
        {Bytes({0xC1, 0x00}), PacketError::SizeTooSmall},
        {Bytes({0xC2, 0x20, 0x01}), PacketError::TooLarge},
    };
    constexpr int GoodFeeds = 2;
    constexpr int Tries = 3;

    for (const Case& test : cases)
    {
        PacketReader reader;
        reader.Feed(Join(test.header, FirstC1));
        const Drained failed = Drain(reader);
        ASSERT_EQ(failed.error, test.error);
        EXPECT_TRUE(failed.packets.empty());
        EXPECT_EQ(reader.Pending(), 0u);

        for (int feed = 0; feed < GoodFeeds; ++feed)
        {
            reader.Feed(FirstC1);
            EXPECT_EQ(reader.Pending(), 0u);
            for (int k = 0; k < Tries; ++k)
            {
                auto next = reader.Next();
                ASSERT_FALSE(next.HasValue()) << "Next() " << k << " después del Feed " << feed;
                EXPECT_EQ(next.Error(), test.error);
            }
        }
    }
}

enum class MoveKind : std::uint8_t
{
    Construct,
    Assign,
};

PacketReader MoveOut(PacketReader& from, MoveKind kind)
{
    if (kind == MoveKind::Construct)
    {
        return PacketReader(std::move(from));
    }
    PacketReader target;
    target.Feed(Bytes({0xC1, 0x03}));
    target = std::move(from);
    return target;
}

// The receiving one goes on where the other was, with its packet half arrived.
TEST(PacketReaderTest, AMovedFromReaderStartsOver)
{
    for (const MoveKind kind : {MoveKind::Construct, MoveKind::Assign})
    {
        PacketReader original;
        original.Feed(Bytes({0xC1, 0x03, 0x00, 0xC1, 0x03}));
        ASSERT_EQ(Drain(original).packets.size(), 1u);

        PacketReader moved = MoveOut(original, kind);

        EXPECT_EQ(original.Pending(), 0u);
        original.Feed(FirstC1);
        const Drained fromOriginal = Drain(original);
        EXPECT_FALSE(fromOriginal.error.has_value());
        ASSERT_EQ(fromOriginal.packets.size(), 1u);
        EXPECT_TRUE(SameBytes(fromOriginal.packets[0], FirstC1));

        EXPECT_EQ(moved.Pending(), 2u);
        moved.Feed(Bytes({0x00}));
        const Drained fromMoved = Drain(moved);
        ASSERT_EQ(fromMoved.packets.size(), 1u);
        EXPECT_TRUE(SameBytes(fromMoved.packets[0], Bytes({0xC1, 0x03, 0x00})));
    }
}

TEST(PacketReaderTest, MovingTakesTheError)
{
    for (const MoveKind kind : {MoveKind::Construct, MoveKind::Assign})
    {
        PacketReader failed;
        failed.Feed(Bytes({0xC5}));
        ASSERT_EQ(Drain(failed).error, PacketError::InvalidType);

        PacketReader moved = MoveOut(failed, kind);

        EXPECT_EQ(Drain(moved).error, PacketError::InvalidType);
        failed.Feed(FirstC1);
        const Drained drained = Drain(failed);
        EXPECT_FALSE(drained.error.has_value());
        ASSERT_EQ(drained.packets.size(), 1u);
        EXPECT_TRUE(SameBytes(drained.packets[0], FirstC1));
    }
}

ByteVector NumberedC1(std::size_t k)
{
    return Bytes({0xC1, 0x04, static_cast<int>(k >> 8), static_cast<int>(k & 0xFF)});
}

// Pending grows and packets still come out in order. Each Feed brings five bytes of four-byte packets, and only one
// is taken.
TEST(PacketReaderTest, KeepsWhatIsNotDrainedBetweenFeeds)
{
    constexpr std::size_t PacketCount = 500;
    constexpr std::size_t ReadSize = 5;

    ByteVector stream;
    for (std::size_t k = 0; k < PacketCount; ++k)
    {
        stream = Join(stream, NumberedC1(k));
    }
    PacketReader reader;
    std::vector<ByteVector> got;
    const std::span<const std::byte> bytes(stream);
    for (std::size_t offset = 0; offset < bytes.size(); offset += ReadSize)
    {
        reader.Feed(bytes.subspan(offset, std::min(ReadSize, bytes.size() - offset)));
        auto next = reader.Next();
        ASSERT_TRUE(next.HasValue());
        if (next.Value())
        {
            const std::span<const std::byte> packet = next.Value()->Bytes();
            got.emplace_back(packet.begin(), packet.end());
        }
    }
    const std::size_t pendingAtTheEnd = reader.Pending();
    for (ByteVector& packet : Drain(reader).packets)
    {
        got.push_back(std::move(packet));
    }

    EXPECT_GT(pendingAtTheEnd, 0u);
    ASSERT_EQ(got.size(), PacketCount);
    for (std::size_t k = 0; k < PacketCount; ++k)
    {
        EXPECT_TRUE(SameBytes(got[k], NumberedC1(k))) << "paquete " << k;
    }
    EXPECT_EQ(reader.Pending(), 0u);
}

TEST(PacketReaderTest, NothingFedGivesNothing)
{
    PacketReader reader;
    reader.Feed({});

    const Drained drained = Drain(reader);

    EXPECT_TRUE(drained.packets.empty());
    EXPECT_FALSE(drained.error.has_value());
}

// C2/C4 go up to 600 so the size's high byte is not always 0.
ByteVector RandomPacket(std::mt19937& engine)
{
    constexpr PacketType Types[] = {PacketType::C1, PacketType::C2, PacketType::C3, PacketType::C4};
    constexpr std::uint32_t MaxOneByteSize = 0xFF;
    constexpr std::uint32_t MaxTwoByteSize = 600;

    const PacketType type = Types[Below(engine, static_cast<std::uint32_t>(std::size(Types)))];
    const bool oneByteSize = type == PacketType::C1 || type == PacketType::C3;
    const std::uint32_t minSize = oneByteSize ? 3 : 4;
    const std::uint32_t maxSize = oneByteSize ? MaxOneByteSize : MaxTwoByteSize;
    ByteVector packet = PacketOfSize(type, minSize + Below(engine, maxSize - minSize + 1), std::byte{0});
    const std::size_t headerSize = oneByteSize ? 2 : 3;
    for (std::size_t i = headerSize; i < packet.size(); ++i)
    {
        packet[i] = static_cast<std::byte>(Below(engine, 0x100));
    }
    return packet;
}

// Each Feed brings a whole read, so a cut can fall on any byte of a header.
TEST(PacketReaderTest, RandomSplitsGiveTheSamePackets)
{
    constexpr std::uint32_t PacketCount = 300;
    constexpr std::uint32_t MaxReadSize = 64;

    std::mt19937 engine(FixedSeed);
    std::vector<ByteVector> expected;
    ByteVector stream;
    for (std::uint32_t k = 0; k < PacketCount; ++k)
    {
        expected.push_back(RandomPacket(engine));
        stream = Join(stream, expected.back());
    }

    PacketReader reader;
    std::vector<ByteVector> got;
    const std::span<const std::byte> bytes(stream);
    for (std::size_t offset = 0; offset < bytes.size();)
    {
        const std::size_t read = std::min<std::size_t>(1 + Below(engine, MaxReadSize), bytes.size() - offset);
        reader.Feed(bytes.subspan(offset, read));
        Drained drained = Drain(reader);
        ASSERT_FALSE(drained.error.has_value());
        for (ByteVector& packet : drained.packets)
        {
            got.push_back(std::move(packet));
        }
        offset += read;
    }

    EXPECT_EQ(got, expected);
    EXPECT_EQ(reader.Pending(), 0u);
}
} // namespace
