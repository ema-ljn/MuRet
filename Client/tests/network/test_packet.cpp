#include "Network/Packet.h"

#include "Network/PacketError.h"
#include "Network/PacketReader.h"
#include "Network/PacketWriter.h"
#include "support/Bytes.h"
#include "support/NetworkPrinters.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <initializer_list>
#include <optional>
#include <utility>
#include <vector>

namespace
{
using Client::Network::AppendHeader;
using Client::Network::CheckDeclaredSize;
using Client::Network::DeclaredSize;
using Client::Network::HeaderSize;
using Client::Network::MaxPacketSize;
using Client::Network::MinPacketSize;
using Client::Network::Packet;
using Client::Network::PacketError;
using Client::Network::PacketType;
using Client::Network::PacketTypeFromByte;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;

std::vector<std::byte> C2OfSize(std::size_t size, std::byte fill)
{
    std::vector<std::byte> bytes(size, fill);
    bytes[0] = std::byte{0xC2};
    bytes[1] = static_cast<std::byte>(size >> 8);
    bytes[2] = static_cast<std::byte>(size & 0xFF);
    return bytes;
}

PacketError ErrorOf(std::vector<std::byte> bytes)
{
    const auto packet = Packet::FromBytes(std::move(bytes));
    EXPECT_FALSE(packet.HasValue());
    return packet.HasValue() ? PacketError{} : packet.Error();
}

TEST(PacketTest, KnowsTheFourTypesAndTheirHeaders)
{
    EXPECT_EQ(PacketTypeFromByte(std::byte{0xC1}), PacketType::C1);
    EXPECT_EQ(PacketTypeFromByte(std::byte{0xC4}), PacketType::C4);
    EXPECT_EQ(PacketTypeFromByte(std::byte{0xC5}), std::nullopt);
    EXPECT_EQ(PacketTypeFromByte(std::byte{0x00}), std::nullopt);

    EXPECT_EQ(HeaderSize(PacketType::C1), 2u);
    EXPECT_EQ(HeaderSize(PacketType::C3), 2u);
    EXPECT_EQ(HeaderSize(PacketType::C2), 3u);
    EXPECT_EQ(HeaderSize(PacketType::C4), 3u);
    EXPECT_EQ(MinPacketSize(PacketType::C1), 3u);
    EXPECT_EQ(MinPacketSize(PacketType::C4), 4u);
}

TEST(PacketTest, ReadsTheDeclaredSize)
{
    EXPECT_EQ(DeclaredSize(PacketType::C1, Bytes({0xC1, 0x04})), 4u);
    EXPECT_EQ(DeclaredSize(PacketType::C2, Bytes({0xC2, 0x01, 0x2C})), 300u);
    EXPECT_EQ(DeclaredSize(PacketType::C2, Bytes({0xC2, 0x01})), std::nullopt);
    EXPECT_EQ(DeclaredSize(PacketType::C3, Bytes({0xC3})), std::nullopt);
}

TEST(PacketTest, AppendsTheHeaderAfterWhatWasThere)
{
    std::vector<std::byte> out = Bytes({0x99});

    AppendHeader(out, PacketType::C1, 4);
    AppendHeader(out, PacketType::C2, 300);
    AppendHeader(out, PacketType::C3, 0xFF);
    AppendHeader(out, PacketType::C4, MaxPacketSize);

    EXPECT_TRUE(SameBytes(out, Bytes({0x99, 0xC1, 0x04, 0xC2, 0x01, 0x2C, 0xC3, 0xFF, 0xC4, 0x20, 0x00})));
}

TEST(PacketTest, AcceptsAC1AndAC2)
{
    const auto c1 = Packet::FromBytes(Bytes({0xC1, 0x04, 0x20, 0xAA}));
    ASSERT_TRUE(c1.HasValue());
    EXPECT_EQ(c1.Value().Type(), PacketType::C1);
    EXPECT_TRUE(SameBytes(c1.Value().Bytes(), Bytes({0xC1, 0x04, 0x20, 0xAA})));

    const auto c2 = Packet::FromBytes(Bytes({0xC2, 0x00, 0x05, 0x52, 0xAA}));
    ASSERT_TRUE(c2.HasValue());
    EXPECT_EQ(c2.Value().Type(), PacketType::C2);
}

TEST(PacketTest, RejectsADeclaredSizeThatIsNotItsSize)
{
    EXPECT_EQ(ErrorOf(Bytes({0xC1, 0x05, 0x20, 0xAA})), PacketError::SizeMismatch);
    EXPECT_EQ(ErrorOf(Bytes({0xC1, 0x03, 0x20, 0xAA})), PacketError::SizeMismatch);
}

TEST(PacketTest, RejectsAnInvalidType)
{
    EXPECT_EQ(ErrorOf(Bytes({0xC5, 0x04, 0x20, 0xAA})), PacketError::InvalidType);
}

// With C1 02 no opcode that is not there is read.
TEST(PacketTest, RejectsWhatIsSmallerThanHeaderAndOpcode)
{
    EXPECT_EQ(ErrorOf({}), PacketError::SizeTooSmall);
    EXPECT_EQ(ErrorOf(Bytes({0xC1})), PacketError::SizeTooSmall);
    EXPECT_EQ(ErrorOf(Bytes({0xC2, 0x00})), PacketError::SizeTooSmall);
    EXPECT_EQ(ErrorOf(Bytes({0xC1, 0x02})), PacketError::SizeTooSmall);
    EXPECT_EQ(ErrorOf(Bytes({0xC2, 0x00, 0x03})), PacketError::SizeTooSmall);
}

// One more is TooLarge even if its size is the one it measures.
TEST(PacketTest, RejectsMoreThanMaxPacketSize)
{
    EXPECT_TRUE(Packet::FromBytes(C2OfSize(MaxPacketSize, std::byte{0x11})).HasValue());
    EXPECT_EQ(ErrorOf(C2OfSize(MaxPacketSize + 1, std::byte{0x11})), PacketError::TooLarge);
}

// C2 20 01 and C2 FF FF are only the header, and C1 02 20 AA measures 4.
TEST(PacketTest, SaysABadDeclaredSizeBeforeAMismatch)
{
    EXPECT_EQ(ErrorOf(Bytes({0xC2, 0x20, 0x01})), PacketError::TooLarge);
    EXPECT_EQ(ErrorOf(Bytes({0xC2, 0xFF, 0xFF})), PacketError::TooLarge);
    EXPECT_EQ(ErrorOf(Bytes({0xC1, 0x02, 0x20, 0xAA})), PacketError::SizeTooSmall);
}

TEST(PacketTest, ChecksTheDeclaredSizeAtTheEdges)
{
    for (const PacketType type : {PacketType::C1, PacketType::C2, PacketType::C3, PacketType::C4})
    {
        EXPECT_EQ(CheckDeclaredSize(type, 0), PacketError::SizeTooSmall);
        EXPECT_EQ(CheckDeclaredSize(type, MinPacketSize(type) - 1), PacketError::SizeTooSmall);
        EXPECT_EQ(CheckDeclaredSize(type, MinPacketSize(type)), std::nullopt);
        EXPECT_EQ(CheckDeclaredSize(type, MaxPacketSize), std::nullopt);
        EXPECT_EQ(CheckDeclaredSize(type, MaxPacketSize + 1), PacketError::TooLarge);
    }
}

TEST(PacketTest, OpcodeOnlyInC1AndC2)
{
    const auto c1 = Packet::FromBytes(Bytes({0xC1, 0x04, 0x20, 0xAA}));
    const auto c2 = Packet::FromBytes(Bytes({0xC2, 0x00, 0x05, 0x52, 0xAA}));
    const auto c3 = Packet::FromBytes(Bytes({0xC3, 0x04, 0x20, 0xAA}));
    const auto c4 = Packet::FromBytes(Bytes({0xC4, 0x00, 0x05, 0x52, 0xAA}));
    ASSERT_TRUE(c1.HasValue() && c2.HasValue() && c3.HasValue() && c4.HasValue());

    EXPECT_EQ(c1.Value().Opcode(), Client::Network::Opcode{0x20});
    EXPECT_EQ(c2.Value().Opcode(), Client::Network::Opcode{0x52});
    EXPECT_EQ(c3.Value().Opcode(), std::nullopt);
    EXPECT_EQ(c4.Value().Opcode(), std::nullopt);
}

TEST(PacketTest, MutableBodyStartsAfterTheHeader)
{
    auto c1 = Packet::FromBytes(Bytes({0xC1, 0x04, 0x20, 0xAA}));
    ASSERT_TRUE(c1.HasValue());
    ASSERT_EQ(c1.Value().MutableBody().size(), 2u);
    EXPECT_EQ(c1.Value().MutableBody()[0], std::byte{0x20});
    c1.Value().MutableBody()[1] = std::byte{0x55};
    EXPECT_TRUE(SameBytes(c1.Value().Bytes(), Bytes({0xC1, 0x04, 0x20, 0x55})));

    auto c2 = Packet::FromBytes(Bytes({0xC2, 0x00, 0x05, 0x52, 0xAA}));
    ASSERT_TRUE(c2.HasValue());
    ASSERT_EQ(c2.Value().MutableBody().size(), 2u);
    EXPECT_EQ(c2.Value().MutableBody()[0], std::byte{0x52});
}

TEST(PacketTest, TakeBytesGivesTheSameBytesBack)
{
    auto loaded = Packet::FromBytes(Bytes({0xC2, 0x00, 0x05, 0x52, 0xAA}));
    ASSERT_TRUE(loaded.HasValue());
    Packet packet = std::move(loaded).Value();

    const std::vector<std::byte> bytes = std::move(packet).TakeBytes();

    EXPECT_TRUE(SameBytes(bytes, Bytes({0xC2, 0x00, 0x05, 0x52, 0xAA})));
    EXPECT_TRUE(Packet::FromBytes(bytes).HasValue());
}

TEST(PacketTest, TakeBytesLeavesItEmpty)
{
    auto loaded = Packet::FromBytes(Bytes({0xC1, 0x04, 0x20, 0xAA}));
    ASSERT_TRUE(loaded.HasValue());
    Packet packet = std::move(loaded).Value();

    const std::vector<std::byte> bytes = std::move(packet).TakeBytes();

    EXPECT_EQ(bytes.size(), 4u);
    EXPECT_TRUE(packet.Bytes().empty());
    EXPECT_TRUE(packet.MutableBody().empty());
    EXPECT_EQ(packet.Opcode(), std::nullopt);
}

TEST(PacketTest, RawBytesAndReaderDoNotClaimDecryption)
{
    for (const auto& bytes : {Bytes({0xC1, 0x04, 0x20, 0xAA}), Bytes({0xC2, 0x00, 0x05, 0x20, 0xAA}),
                              Bytes({0xC3, 0x04, 0x20, 0xAA}), Bytes({0xC4, 0x00, 0x05, 0x20, 0xAA})})
    {
        const auto packet = Packet::FromBytes(bytes);
        ASSERT_TRUE(packet.HasValue());
        EXPECT_FALSE(packet.Value().WasDecrypted());
        Client::Network::PacketReader reader;
        reader.Feed(bytes);
        const auto cut = reader.Next();
        ASSERT_TRUE(cut.HasValue());
        ASSERT_TRUE(cut.Value().has_value());
        EXPECT_FALSE(cut.Value()->WasDecrypted());
        EXPECT_TRUE(SameBytes(cut.Value()->Bytes(), bytes));
    }
}

TEST(PacketTest, WriterDoesNotClaimDecryption)
{
    for (const auto& writer : {Client::Network::PacketWriter::C1(Client::Network::Opcode{0x20}),
                               Client::Network::PacketWriter::C2(Client::Network::Opcode{0x20})})
    {
        const auto packet = writer.Finish();
        ASSERT_TRUE(packet.HasValue());
        EXPECT_FALSE(packet.Value().WasDecrypted());
    }
}

// The explicit bool only carries metadata: FromBytes does no cryptography.
TEST(PacketTest, CopiesAndMovesPreserveDecryptionProvenance)
{
    const auto bytes = Bytes({0xC1, 0x04, 0x20, 0xAA});
    for (const bool wasDecrypted : {false, true})
    {
        auto built = Packet::FromBytes(bytes, wasDecrypted);
        ASSERT_TRUE(built.HasValue());
        const Packet original = std::move(built).Value();
        Packet copied = original;
        Packet moved = std::move(copied);
        EXPECT_EQ(original.WasDecrypted(), wasDecrypted);
        EXPECT_EQ(moved.WasDecrypted(), wasDecrypted);
        EXPECT_EQ(moved.Type(), PacketType::C1);
        EXPECT_TRUE(SameBytes(moved.Bytes(), bytes));

        auto opposite = Packet::FromBytes(bytes, !wasDecrypted);
        ASSERT_TRUE(opposite.HasValue());
        Packet assigned = std::move(opposite).Value();
        assigned = original;
        EXPECT_EQ(assigned.WasDecrypted(), wasDecrypted);
        Packet moveAssigned = Packet::FromBytes(bytes, !wasDecrypted).Value();
        moveAssigned = std::move(moved);
        EXPECT_EQ(moveAssigned.WasDecrypted(), wasDecrypted);
        EXPECT_TRUE(SameBytes(moveAssigned.Bytes(), bytes));
    }
}
} // namespace
