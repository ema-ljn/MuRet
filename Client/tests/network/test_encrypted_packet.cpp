#include "Network/EncryptedPacket.h"

#include "Core/Expected.h"
#include "Crypto/CryptoError.h"
#include "Crypto/SimpleModulus.h"
#include "Network/IObfuscation.h"
#include "Network/ObfuscationError.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"

#include "support/BytePattern.h"
#include "support/Bytes.h"
#include "support/FixedRandom.h"
#include "support/MuClientNetFixtures.h"
#include "support/NetVectors.h"
#include "support/NetworkPrinters.h"
#include "support/Packets.h"
#include "support/SameBytes.h"
#include "support/SimpleModulusKeys.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <random>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using Client::Core::Expected;
using Client::Crypto::CryptoError;
using Client::Crypto::DecryptBlock;
using Client::Crypto::DecryptedBlock;
using Client::Crypto::EncryptBlock;
using Client::Crypto::EncryptionKey;
using Client::Crypto::PlainBlockSize;
using Client::Network::DecryptPacket;
using Client::Network::EncryptPacket;
using Client::Network::HeaderSize;
using Client::Network::IObfuscation;
using Client::Network::MaxPacketSize;
using Client::Network::MinPacketSize;
using Client::Network::ObfuscationError;
using Client::Network::Packet;
using Client::Network::PacketSerials;
using Client::Network::PacketType;
using Client::Network::SerialCheck;
using Client::Tests::Below;
using Client::Tests::BytePattern;
using Client::Tests::Bytes;
using Client::Tests::FixedSeed;
using Client::Tests::IdentityPair;
using Client::Tests::KeyPair;
using Client::Tests::NoticeBytes;
using Client::Tests::NoticeRandA5;
using Client::Tests::NoticeSerial0;
using Client::Tests::NoticeSerialFF;
using Client::Tests::PacketOf;
using Client::Tests::Pair17;
using Client::Tests::Pair18;
using Client::Tests::ReadNetVector;
using Client::Tests::SameBytes;

using ByteVector = std::vector<std::byte>;

// The opcode of the CL2.0 examples and of c2-long.bin.
constexpr std::byte TestOpcode{0x42};

// Not the expected serial, 0: if an error resynced with the serial it carried instead of spending the expected one,
// the next would be 0x38 and not 1.
constexpr int OtherSerial = 0x37;

// Byte 1 in a C1/C3, bytes 1 and 2 big-endian in a C2/C4.
void PutHeader(ByteVector& bytes, PacketType type)
{
    const std::size_t size = bytes.size();
    bytes[0] = static_cast<std::byte>(type);
    if (HeaderSize(type) == 2)
    {
        bytes[1] = static_cast<std::byte>(size);
        return;
    }
    bytes[1] = static_cast<std::byte>(size >> 8);
    bytes[2] = static_cast<std::byte>(size & 0xFF);
}

// The header, TestOpcode and the rest with BytePattern(…, 37, 11), c2-long.bin's pattern.
ByteVector PlainOfSize(PacketType type, std::size_t size)
{
    ByteVector bytes = BytePattern(size, 37, 11);
    PutHeader(bytes, type);
    bytes[HeaderSize(type)] = TestOpcode;
    return bytes;
}

// From the opcode in a C1/C2.
std::span<const std::byte> AfterHeader(const Packet& packet)
{
    return packet.Bytes().subspan(HeaderSize(packet.Type()));
}

PacketSerials HavingSent(int sent)
{
    PacketSerials serials;
    for (int k = 0; k < sent; ++k)
    {
        [[maybe_unused]] const std::uint8_t serial = serials.TakeSend();
    }
    return serials;
}

PacketSerials Expecting(int expected)
{
    PacketSerials serials;
    for (int k = 0; k < expected; ++k)
    {
        serials.SkipReceive();
    }
    return serials;
}

struct PlainBlock
{
    std::array<std::byte, PlainBlockSize> bytes{};
    std::uint8_t useful = 0;
};

// The missing bytes up to 8 are 0.
PlainBlock BlockOf(std::initializer_list<int> values, std::uint8_t useful)
{
    EXPECT_LE(values.size(), PlainBlockSize);
    const ByteVector given = Bytes(values);
    PlainBlock block;
    std::copy_n(given.begin(), std::min(given.size(), PlainBlockSize), block.bytes.begin());
    block.useful = useful;
    return block;
}

// Built block by block with EncryptBlock, without EncryptPacket, so a test can set any useful bytes, also those
// EncryptPacket never writes.
Packet EncryptedOf(PacketType type, const EncryptionKey& key, const std::vector<PlainBlock>& blocks)
{
    ByteVector bytes(HeaderSize(type));
    for (const PlainBlock& block : blocks)
    {
        const auto cipher = EncryptBlock(key, block.bytes, block.useful);
        bytes.insert(bytes.end(), cipher.begin(), cipher.end());
    }
    PutHeader(bytes, type);
    return PacketOf(std::move(bytes));
}

// FIX_C3_SERIAL0: the serial overwrites the C1's size, and encryption starts there.
TEST(EncryptedPacketTest, EncryptsTheNoticeWithSerial0)
{
    const KeyPair identity = IdentityPair();
    PacketSerials serials;

    const auto encrypted = EncryptPacket(PacketOf(NoticeBytes()), identity.encryption, serials);

    ASSERT_TRUE(encrypted.HasValue());
    EXPECT_EQ(encrypted.Value().Type(), PacketType::C3);
    EXPECT_TRUE(SameBytes(encrypted.Value().Bytes(), NoticeSerial0()));
    EXPECT_EQ(serials.TakeSend(), 1);
}

// FIX_C3_RANDA5: the padding is the byte after the useful ones.
TEST(EncryptedPacketTest, EncryptsTheNoticeWithItsPadding)
{
    const KeyPair identity = IdentityPair();
    PacketSerials serials;

    const auto encrypted = EncryptPacket(PacketOf(NoticeBytes()), identity.encryption, serials, std::byte{0xA5});

    ASSERT_TRUE(encrypted.HasValue());
    EXPECT_TRUE(SameBytes(encrypted.Value().Bytes(), NoticeRandA5()));
}

// FIX_C3_SERIALFF: packet 256 leaves with serial 255, and the next with 0.
TEST(EncryptedPacketTest, EncryptsTheNoticeWithSerial255AndWraps)
{
    const KeyPair identity = IdentityPair();
    PacketSerials serials = HavingSent(255);

    const auto encrypted = EncryptPacket(PacketOf(NoticeBytes()), identity.encryption, serials);

    ASSERT_TRUE(encrypted.HasValue());
    EXPECT_TRUE(SameBytes(encrypted.Value().Bytes(), NoticeSerialFF()));
    EXPECT_EQ(serials.TakeSend(), 0);
}

// A C3 from mu-client and the serial it left with.
struct NoticeFixture
{
    std::string_view name;
    ByteVector encrypted;
    int serial = 0;
};

TEST(EncryptedPacketTest, DecryptsTheMuClientFixturesIntoTheNotice)
{
    const KeyPair identity = IdentityPair();
    const std::vector<NoticeFixture> fixtures{
        {"FIX_C3_SERIAL0", NoticeSerial0(), 0},
        {"FIX_C3_RANDA5", NoticeRandA5(), 0},
        {"FIX_C3_SERIALFF", NoticeSerialFF(), 255},
    };

    for (const NoticeFixture& fixture : fixtures)
    {
        PacketSerials serials = Expecting(fixture.serial);

        const auto decrypted = DecryptPacket(PacketOf(fixture.encrypted), identity.decryption, serials);

        ASSERT_TRUE(decrypted.HasValue()) << fixture.name;
        EXPECT_EQ(decrypted.Value().serial, SerialCheck::InOrder) << fixture.name;
        EXPECT_TRUE(decrypted.Value().packet.WasDecrypted()) << fixture.name;
        EXPECT_EQ(decrypted.Value().packet.Type(), PacketType::C1) << fixture.name;
        EXPECT_TRUE(SameBytes(decrypted.Value().packet.Bytes(), NoticeBytes())) << fixture.name;
    }
}

// CL2.2's long C2 (c2-long.bin) with serial 2 gives c4-serial2.bin, mu-client's FIX_C4_SERIAL2 rebuilt: 298 useful
// bytes are 38 blocks, 418 encrypted bytes and 421 with the header.
TEST(EncryptedPacketTest, EncryptsTheLongC2AsC4WithSerial2)
{
    const KeyPair identity = IdentityPair();
    const ByteVector c2 = ReadNetVector("c2-long.bin");
    const ByteVector c4 = ReadNetVector("c4-serial2.bin");
    ASSERT_FALSE(c2.empty());
    ASSERT_FALSE(c4.empty());
    PacketSerials serials = HavingSent(2);

    const auto encrypted = EncryptPacket(PacketOf(c2), identity.encryption, serials);

    ASSERT_TRUE(encrypted.HasValue());
    EXPECT_EQ(encrypted.Value().Type(), PacketType::C4);
    EXPECT_TRUE(SameBytes(encrypted.Value().Bytes(), c4));
}

// C4 gives C2, with the size in two bytes.
TEST(EncryptedPacketTest, DecryptsTheC4IntoTheLongC2)
{
    const KeyPair identity = IdentityPair();
    const ByteVector c2 = ReadNetVector("c2-long.bin");
    const ByteVector c4 = ReadNetVector("c4-serial2.bin");
    ASSERT_FALSE(c2.empty());
    ASSERT_FALSE(c4.empty());
    PacketSerials serials = Expecting(2);

    const auto decrypted = DecryptPacket(PacketOf(c4), identity.decryption, serials);

    ASSERT_TRUE(decrypted.HasValue());
    EXPECT_EQ(decrypted.Value().serial, SerialCheck::InOrder);
    EXPECT_TRUE(SameBytes(decrypted.Value().packet.Bytes(), c2));
    EXPECT_TRUE(decrypted.Value().packet.WasDecrypted());
    EXPECT_EQ(decrypted.Value().packet.Type(), PacketType::C2);
}

// With a synthetic pair and three blocks, what make_vectors.py's `wrap` gives with c3c4_reference.py's
// encrypt_block: a 20-byte C1 is 19 useful (8, 8 and 3), with serial 7 and padding 0x5C in the third block's byte 3.
TEST(EncryptedPacketTest, EncryptsThreeBlocksLikeTheReference)
{
    const KeyPair pair = Pair17();
    const ByteVector plain = Bytes({0xC1, 0x14, 0x42, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                                    0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10});
    const ByteVector expected = Bytes({0xC3, 0x23, 0xF2, 0x99, 0x16, 0x68, 0xC2, 0x59, 0x64, 0x6B, 0x44, 0x89,
                                       0xBC, 0x36, 0x59, 0x55, 0x3A, 0x0D, 0x77, 0x51, 0x68, 0x38, 0xCD, 0xF8,
                                       0xBC, 0x71, 0x45, 0x43, 0xC1, 0x8D, 0x33, 0x48, 0x84, 0x8B, 0xB5});
    PacketSerials serials = HavingSent(7);

    const auto encrypted = EncryptPacket(PacketOf(plain), pair.encryption, serials, std::byte{0x5C});

    ASSERT_TRUE(encrypted.HasValue());
    EXPECT_TRUE(SameBytes(encrypted.Value().Bytes(), expected));
}

// After the useful bytes goes the padding, and the rest of the last block is 0: the notice has 5 useful bytes, so its
// plain block is [serial F1 7B C1 73], the padding and two zeros.
TEST(EncryptedPacketTest, PutsThePaddingRightAfterTheUsefulBytes)
{
    const KeyPair identity = IdentityPair();
    PacketSerials serials;

    const auto encrypted = EncryptPacket(PacketOf(NoticeBytes()), identity.encryption, serials, std::byte{0xA5});

    ASSERT_TRUE(encrypted.HasValue());
    const std::span<const std::byte> cipher = AfterHeader(encrypted.Value());
    ASSERT_EQ(cipher.size(), Client::Crypto::CipherBlockSize);
    const std::optional<DecryptedBlock> block =
        DecryptBlock(identity.decryption, cipher.first<Client::Crypto::CipherBlockSize>());
    ASSERT_TRUE(block.has_value());
    EXPECT_TRUE(SameBytes(block->bytes, Bytes({0x00, 0xF1, 0x7B, 0xC1, 0x73, 0xA5, 0x00, 0x00})));
    EXPECT_EQ(block->usefulSize, 5);
}

// A full block has no room for the padding: a 9-byte C1 is 8 useful, and the padding changes nothing.
TEST(EncryptedPacketTest, AFullBlockHasNoPadding)
{
    const KeyPair identity = IdentityPair();
    const Packet plain = PacketOf(PlainOfSize(PacketType::C1, 9));
    PacketSerials withZero;
    PacketSerials withA5;

    const auto zero = EncryptPacket(plain, identity.encryption, withZero);
    const auto a5 = EncryptPacket(plain, identity.encryption, withA5, std::byte{0xA5});

    ASSERT_TRUE(zero.HasValue());
    ASSERT_TRUE(a5.HasValue());
    EXPECT_EQ(zero.Value().Bytes().size(), 13u);
    EXPECT_TRUE(SameBytes(a5.Value().Bytes(), zero.Value().Bytes()));
}

// A C1's C3/C4 edge, by its encrypted size (cmp eax,0x100 at 0x4e22a8): one of 185 bytes is 184 useful, 23 blocks and
// 253 encrypted, below 0x100, and leaves as a C3 of 255; one of 186 is 24 blocks, 264 encrypted, and leaves as a C4
// of 267.
TEST(EncryptedPacketTest, AC1GoesOutAsC3UpTo23Blocks)
{
    const KeyPair identity = IdentityPair();
    PacketSerials serials;

    const auto c3 = EncryptPacket(PacketOf(PlainOfSize(PacketType::C1, 185)), identity.encryption, serials);
    const auto c4 = EncryptPacket(PacketOf(PlainOfSize(PacketType::C1, 186)), identity.encryption, serials);

    ASSERT_TRUE(c3.HasValue());
    ASSERT_TRUE(c4.HasValue());
    EXPECT_EQ(c3.Value().Type(), PacketType::C3);
    EXPECT_EQ(c3.Value().Bytes().size(), 255u);
    EXPECT_TRUE(SameBytes(c3.Value().Bytes().first(2), Bytes({0xC3, 0xFF})));
    EXPECT_EQ(c4.Value().Type(), PacketType::C4);
    EXPECT_EQ(c4.Value().Bytes().size(), 267u);
    EXPECT_TRUE(SameBytes(c4.Value().Bytes().first(3), Bytes({0xC4, 0x01, 0x0B})));
}

// A C1 that leaves as a C4 comes back as a C2, with one more header byte: a C4 is rebuilt as a C2 (0x4e1d50), as in
// the original. What follows the header is the same.
TEST(EncryptedPacketTest, AC1ThatGoesOutAsC4ComesBackAsC2)
{
    const KeyPair identity = IdentityPair();
    const Packet plain = PacketOf(PlainOfSize(PacketType::C1, 186));
    PacketSerials serials;

    const auto encrypted = EncryptPacket(plain, identity.encryption, serials);
    ASSERT_TRUE(encrypted.HasValue());
    const auto decrypted = DecryptPacket(encrypted.Value(), identity.decryption, serials);

    ASSERT_TRUE(decrypted.HasValue());
    EXPECT_EQ(decrypted.Value().packet.Type(), PacketType::C2);
    EXPECT_EQ(decrypted.Value().packet.Bytes().size(), 187u);
    EXPECT_TRUE(SameBytes(AfterHeader(decrypted.Value().packet), AfterHeader(plain)));
}

// A C2 always leaves as a C4, even if the encrypted part is below 0x100: that is how the letter window builds it
// (0x498820, 0x49941e's mov 0xc4, without the cmp), the only sender of a C2. One of 6 bytes is 4 useful, one block,
// and leaves as a C4 of 14; it comes back as the same C2.
TEST(EncryptedPacketTest, AC2AlwaysGoesOutAsC4)
{
    const KeyPair identity = IdentityPair();
    const ByteVector plain = PlainOfSize(PacketType::C2, 6);
    PacketSerials serials;

    const auto encrypted = EncryptPacket(PacketOf(plain), identity.encryption, serials);
    ASSERT_TRUE(encrypted.HasValue());
    const auto decrypted = DecryptPacket(encrypted.Value(), identity.decryption, serials);

    EXPECT_EQ(encrypted.Value().Type(), PacketType::C4);
    EXPECT_TRUE(SameBytes(encrypted.Value().Bytes().first(3), Bytes({0xC4, 0x00, 0x0E})));
    ASSERT_TRUE(decrypted.HasValue());
    EXPECT_TRUE(SameBytes(decrypted.Value().packet.Bytes(), plain));
}

// Q-C05: the largest C2 that is encrypted, 5,954 bytes: 5,952 useful, 744 blocks and a C4 of 8,187, the largest that
// fits in MaxPacketSize. It decrypts whole, in more bytes than the ~2,046 of the stack where the original decrypts.
TEST(EncryptedPacketTest, TheLargestC4DecryptsWhole)
{
    constexpr std::size_t LargestC2 = 5954; // the largest C2 whose C4 fits in MaxPacketSize
    constexpr std::size_t LargestC4 = 8187; // that C4: 744 blocks of 11 and the header
    const KeyPair identity = IdentityPair();
    const ByteVector plain = PlainOfSize(PacketType::C2, LargestC2);
    PacketSerials serials;

    const auto encrypted = EncryptPacket(PacketOf(plain), identity.encryption, serials);
    ASSERT_TRUE(encrypted.HasValue());
    const auto decrypted = DecryptPacket(encrypted.Value(), identity.decryption, serials);

    static_assert(LargestC4 <= MaxPacketSize);
    EXPECT_EQ(encrypted.Value().Bytes().size(), LargestC4);
    ASSERT_TRUE(decrypted.HasValue());
    EXPECT_TRUE(SameBytes(decrypted.Value().packet.Bytes(), plain));
}

// 5,955 would give a C4 of 8,198, over MaxPacketSize: TooLarge, and the serial is not spent.
TEST(EncryptedPacketTest, AC2WhoseC4WouldNotFitIsTooLarge)
{
    constexpr std::size_t TooLargeC2 = 5955;
    const KeyPair identity = IdentityPair();
    PacketSerials serials;

    const auto encrypted =
        EncryptPacket(PacketOf(PlainOfSize(PacketType::C2, TooLargeC2)), identity.encryption, serials);

    ASSERT_FALSE(encrypted.HasValue());
    EXPECT_EQ(encrypted.Error(), CryptoError::TooLarge);
    EXPECT_EQ(serials.TakeSend(), 0);
}

// Q-C10: the C3 of [05] F1 02 01 is rebuilt as C1 05 F1 02 01, which declares what it measures. The original declares
// C1 04, one less: it counts the serial, which it does not copy (0x4e1d50). The C4 of the same gives a C2 of 6, and
// the original declares 4.
TEST(EncryptedPacketTest, TheRebuiltPacketDeclaresWhatItMeasures)
{
    const KeyPair identity = IdentityPair();
    const std::vector<PlainBlock> blocks{BlockOf({0x05, 0xF1, 0x02, 0x01}, 4)};
    PacketSerials fromC3 = Expecting(5);
    PacketSerials fromC4 = Expecting(5);

    const auto c1 =
        DecryptPacket(EncryptedOf(PacketType::C3, identity.encryption, blocks), identity.decryption, fromC3);
    const auto c2 =
        DecryptPacket(EncryptedOf(PacketType::C4, identity.encryption, blocks), identity.decryption, fromC4);

    ASSERT_TRUE(c1.HasValue());
    EXPECT_EQ(c1.Value().serial, SerialCheck::InOrder);
    EXPECT_TRUE(SameBytes(c1.Value().packet.Bytes(), Bytes({0xC1, 0x05, 0xF1, 0x02, 0x01})));
    ASSERT_TRUE(c2.HasValue());
    EXPECT_TRUE(SameBytes(c2.Value().packet.Bytes(), Bytes({0xC2, 0x00, 0x06, 0xF1, 0x02, 0x01})));
}

// Q-C04: a packet with another serial still decrypts and is dispatched, and says Resynced. Expecting 0, 255 arrives;
// the next one, 0, is already in order.
TEST(EncryptedPacketTest, AnotherSerialStillDecryptsAndResyncs)
{
    const KeyPair identity = IdentityPair();
    PacketSerials serials;

    const auto first = DecryptPacket(PacketOf(NoticeSerialFF()), identity.decryption, serials);
    const auto second = DecryptPacket(PacketOf(NoticeSerial0()), identity.decryption, serials);

    ASSERT_TRUE(first.HasValue());
    EXPECT_EQ(first.Value().serial, SerialCheck::Resynced);
    EXPECT_TRUE(first.Value().packet.WasDecrypted());
    EXPECT_TRUE(SameBytes(first.Value().packet.Bytes(), NoticeBytes()));
    ASSERT_TRUE(second.HasValue());
    EXPECT_EQ(second.Value().serial, SerialCheck::InOrder);
    EXPECT_TRUE(second.Value().packet.WasDecrypted());
}

// And spent the expected serial, 0: the next is 1, even if the packet carries OtherSerial.
void ExpectFailedAndSpentTheSerial(const Packet& encrypted, CryptoError error, std::string_view name)
{
    const KeyPair identity = IdentityPair();
    PacketSerials serials;

    const auto decrypted = DecryptPacket(encrypted, identity.decryption, serials);

    ASSERT_FALSE(decrypted.HasValue()) << name;
    EXPECT_EQ(decrypted.Error(), error) << name;
    EXPECT_EQ(serials.Receive(1), SerialCheck::InOrder) << name;
}

// One bit flipped in the notice's checksum (its last byte, like red_reference.py's case 6): ChecksumMismatch. The
// original does not dispatch, adds 1 to the serial and sends the notice (03, "C3/C4").
TEST(EncryptedPacketTest, ABadChecksumSpendsTheSerial)
{
    ByteVector bad = NoticeSerial0();
    bad.back() ^= std::byte{0x01};

    ExpectFailedAndSpentTheSerial(PacketOf(std::move(bad)), CryptoError::ChecksumMismatch, "C3");
}

// The original reads 11 at a time without checking there are enough, and the checksum almost surely fails.
TEST(EncryptedPacketTest, CipherThatIsNotWholeBlocksSpendsTheSerial)
{
    for (const PacketType type : {PacketType::C3, PacketType::C4})
    {
        for (const std::size_t cipherSize : {std::size_t{10}, std::size_t{12}})
        {
            ByteVector bytes(HeaderSize(type) + cipherSize, std::byte{0x5A});
            PutHeader(bytes, type);

            ExpectFailedAndSpentTheSerial(PacketOf(std::move(bytes)), CryptoError::NotWholeBlocks,
                                          type == PacketType::C3 ? "C3" : "C4");
        }
    }
}

struct BlocksCase
{
    std::string_view name;
    std::vector<PlainBlock> blocks;
};

// OtherSerial, the notice and three more.
PlainBlock FirstBlock(std::uint8_t useful)
{
    return BlockOf({OtherSerial, 0xF1, 0x7B, 0xC1, 0x73, 0x01, 0x02, 0x03}, useful);
}

// The checksum does not cover the useful length. The original adds what they say without checking, dispatches and
// compares the serial. With 255, copying the block's useful bytes would go past its 8: ASan sees it if the length is
// not checked first, in the last block and in a middle one. With the last saying 0, the first block passed and
// carries its serial, and the error still spends the expected one.
TEST(EncryptedPacketTest, ABlockWithAnImpossibleUsefulSizeSpendsTheSerial)
{
    constexpr std::uint8_t LargestUsefulSize = 0xFF; // the most the useful length byte can say
    const KeyPair identity = IdentityPair();
    const PlainBlock tail = BlockOf({0x11, 0x22}, 2);
    const std::vector<BlocksCase> cases{
        {"uno que dice 9", {FirstBlock(9)}},
        {"el último que dice 255", {FirstBlock(8), BlockOf({0x11, 0x22}, LargestUsefulSize)}},
        {"uno del medio que dice 7", {FirstBlock(7), tail}},
        {"uno del medio que dice 9", {FirstBlock(9), tail}},
        {"uno del medio que dice 255", {FirstBlock(LargestUsefulSize), tail}},
        {"el último que dice 0", {FirstBlock(8), BlockOf({}, 0)}},
    };

    for (const BlocksCase& blocksCase : cases)
    {
        ExpectFailedAndSpentTheSerial(EncryptedOf(PacketType::C3, identity.encryption, blocksCase.blocks),
                                      CryptoError::InvalidBlock, blocksCase.name);
    }
}

// The original stops at the first bad checksum without looking at the lengths, and it is the error the notice
// carries.
TEST(EncryptedPacketTest, ABadChecksumWinsOverAnInvalidBlock)
{
    const KeyPair identity = IdentityPair();
    const Packet blocks = EncryptedOf(PacketType::C3, identity.encryption, {FirstBlock(9), BlockOf({0x11, 0x22}, 2)});
    ByteVector bad(blocks.Bytes().begin(), blocks.Bytes().end());
    bad.back() ^= std::byte{0x01};

    ExpectFailedAndSpentTheSerial(PacketOf(std::move(bad)), CryptoError::ChecksumMismatch, "C3");
}

// The block passed its checksum and its serial, OtherSerial, is known: the expected one is spent anyway.
TEST(EncryptedPacketTest, OnlyTheSerialIsTooShort)
{
    const KeyPair identity = IdentityPair();
    const std::vector<PlainBlock> blocks{BlockOf({OtherSerial}, 1)};

    ExpectFailedAndSpentTheSerial(EncryptedOf(PacketType::C3, identity.encryption, blocks), CryptoError::TooShort,
                                  "C3");
    ExpectFailedAndSpentTheSerial(EncryptedOf(PacketType::C4, identity.encryption, blocks), CryptoError::TooShort,
                                  "C4");
}

TEST(EncryptedPacketTest, EncryptingAC3OrC4IsTheWrongType)
{
    const KeyPair identity = IdentityPair();
    const ByteVector c4 = ReadNetVector("c4-serial2.bin");
    ASSERT_FALSE(c4.empty());
    PacketSerials serials;

    const auto fromC3 = EncryptPacket(PacketOf(NoticeSerial0()), identity.encryption, serials);
    const auto fromC4 = EncryptPacket(PacketOf(c4), identity.encryption, serials);

    ASSERT_FALSE(fromC3.HasValue());
    EXPECT_EQ(fromC3.Error(), CryptoError::WrongPacketType);
    ASSERT_FALSE(fromC4.HasValue());
    EXPECT_EQ(fromC4.Error(), CryptoError::WrongPacketType);
    EXPECT_EQ(serials.TakeSend(), 0);
}

TEST(EncryptedPacketTest, DecryptingAC1OrC2IsTheWrongType)
{
    const KeyPair identity = IdentityPair();
    PacketSerials serials;

    const auto fromC1 = DecryptPacket(PacketOf(NoticeBytes()), identity.decryption, serials);
    const auto fromC2 = DecryptPacket(PacketOf(PlainOfSize(PacketType::C2, 6)), identity.decryption, serials);

    ASSERT_FALSE(fromC1.HasValue());
    EXPECT_EQ(fromC1.Error(), CryptoError::WrongPacketType);
    ASSERT_FALSE(fromC2.HasValue());
    EXPECT_EQ(fromC2.Error(), CryptoError::WrongPacketType);
    EXPECT_EQ(serials.Receive(0), SerialCheck::InOrder);
}

// A Packet whose bytes were taken keeps its type and is empty.
TEST(EncryptedPacketTest, AnEmptiedPacketIsTheWrongType)
{
    const KeyPair identity = IdentityPair();
    Packet c1 = PacketOf(NoticeBytes());
    Packet c3 = PacketOf(NoticeSerial0());
    [[maybe_unused]] const ByteVector takenFromC1 = std::move(c1).TakeBytes();
    [[maybe_unused]] const ByteVector takenFromC3 = std::move(c3).TakeBytes();
    PacketSerials serials;

    const auto encrypted = EncryptPacket(c1, identity.encryption, serials);
    const auto decrypted = DecryptPacket(c3, identity.decryption, serials);

    ASSERT_FALSE(encrypted.HasValue());
    EXPECT_EQ(encrypted.Error(), CryptoError::WrongPacketType);
    ASSERT_FALSE(decrypted.HasValue());
    EXPECT_EQ(decrypted.Error(), CryptoError::WrongPacketType);
    EXPECT_EQ(serials.TakeSend(), 0);
    EXPECT_EQ(serials.Receive(0), SerialCheck::InOrder);
}

// A 3-byte C1 and a 4-byte C2 are 2 useful, the serial and the opcode: the least DecryptPacket accepts.
TEST(EncryptedPacketTest, TheSmallestPacketsGoAndComeBack)
{
    const KeyPair pair = Pair17();
    for (const PacketType type : {PacketType::C1, PacketType::C2})
    {
        const std::string_view name = type == PacketType::C1 ? "C1" : "C2";
        const ByteVector plain = PlainOfSize(type, MinPacketSize(type));
        PacketSerials serials;

        const auto encrypted = EncryptPacket(PacketOf(plain), pair.encryption, serials);
        ASSERT_TRUE(encrypted.HasValue()) << name;
        const auto decrypted = DecryptPacket(encrypted.Value(), pair.decryption, serials);

        ASSERT_TRUE(decrypted.HasValue()) << name;
        EXPECT_EQ(decrypted.Value().serial, SerialCheck::InOrder) << name;
        EXPECT_TRUE(SameBytes(decrypted.Value().packet.Bytes(), plain)) << name;
    }
}

constexpr std::uint32_t ByteValues = 256;

ByteVector RandomPlain(std::mt19937& engine, PacketType type, std::size_t largest)
{
    const std::size_t smallest = MinPacketSize(type);
    const std::size_t size = smallest + Below(engine, static_cast<std::uint32_t>(largest - smallest + 1));
    ByteVector bytes(size);
    for (std::byte& value : bytes)
    {
        value = static_cast<std::byte>(Below(engine, ByteValues));
    }
    PutHeader(bytes, type);
    return bytes;
}

TEST(EncryptedPacketTest, DecryptUndoesEncryptWithSyntheticKeys)
{
    constexpr std::uint32_t Rounds = 600;             // over 256: the serials wrap
    constexpr std::size_t LargestC1ThatStaysC1 = 185; // a larger one leaves as C4 and comes back as C2
    constexpr std::size_t LargestRandomC2 = 700;      // over 0x100: the high byte is not 0
    std::mt19937 engine(FixedSeed);

    for (const KeyPair& pair : {Pair17(), Pair18()})
    {
        PacketSerials sender;
        PacketSerials receiver;
        for (std::uint32_t round = 0; round < Rounds; ++round)
        {
            const bool isC1 = Below(engine, 2) == 0;
            const ByteVector plain = isC1 ? RandomPlain(engine, PacketType::C1, LargestC1ThatStaysC1)
                                          : RandomPlain(engine, PacketType::C2, LargestRandomC2);
            const auto padding = static_cast<std::byte>(Below(engine, ByteValues));

            const auto encrypted = EncryptPacket(PacketOf(plain), pair.encryption, sender, padding);
            ASSERT_TRUE(encrypted.HasValue()) << "vuelta " << round;
            const auto decrypted = DecryptPacket(encrypted.Value(), pair.decryption, receiver);

            ASSERT_TRUE(decrypted.HasValue()) << "vuelta " << round;
            EXPECT_EQ(decrypted.Value().serial, SerialCheck::InOrder) << "vuelta " << round;
            EXPECT_TRUE(SameBytes(decrypted.Value().packet.Bytes(), plain)) << "vuelta " << round;
        }
    }
}

// Records what EncryptPacket gives it to encrypt and changes the opcode, to show what BeforeEncrypt receives and that
// what it leaves is encrypted.
class RecordingObfuscation final : public IObfuscation
{
public:
    static constexpr std::byte WrittenOpcode{0x5A};

    explicit RecordingObfuscation(ByteVector& seenBytes) : m_seen(&seenBytes) {}

    [[nodiscard]] std::byte InboundMask(std::byte /*firstWireByte*/) const override
    {
        return std::byte{0};
    }

    [[nodiscard]] Expected<Packet, ObfuscationError> AfterCut(Packet packet) const override
    {
        return packet;
    }

    [[nodiscard]] Expected<Packet, ObfuscationError> BeforeDispatch(Packet packet) const override
    {
        return packet;
    }

    void BeforeEncrypt(std::span<std::byte> sealed) const override
    {
        m_seen->assign(sealed.begin(), sealed.end());
        if (sealed.size() > 1)
        {
            sealed[1] = WrittenOpcode;
        }
    }

    [[nodiscard]] Packet BeforeSend(Packet packet) const override
    {
        return packet;
    }

private:
    ByteVector* m_seen;
};

// [serial][useful bytes from the opcode][padding]: the notice, with serial 4 and padding A5, is the 5 useful bytes
// and the padding.
TEST(EncryptedPacketTest, BeforeEncryptGetsTheSerialTheUsefulBytesAndThePadding)
{
    const KeyPair identity = IdentityPair();
    PacketSerials serials = HavingSent(4);
    ByteVector seen;
    const RecordingObfuscation recording(seen);

    const auto encrypted =
        EncryptPacket(PacketOf(NoticeBytes()), identity.encryption, serials, recording, std::byte{0xA5});

    EXPECT_TRUE(SameBytes(seen, Bytes({0x04, 0xF1, 0x7B, 0xC1, 0x73, 0xA5})));
    ASSERT_TRUE(encrypted.HasValue());
    const std::span<const std::byte> cipher = AfterHeader(encrypted.Value());
    ASSERT_EQ(cipher.size(), Client::Crypto::CipherBlockSize);
    const std::optional<DecryptedBlock> block =
        DecryptBlock(identity.decryption, cipher.first<Client::Crypto::CipherBlockSize>());
    ASSERT_TRUE(block.has_value());
    EXPECT_TRUE(SameBytes(block->bytes, Bytes({0x04, 0x5A, 0x7B, 0xC1, 0x73, 0xA5, 0x00, 0x00})));
    EXPECT_EQ(block->usefulSize, 5);
}

// Even with the block full the padding reaches BeforeEncrypt, though it is not encrypted: a 9-byte C1 and a 10-byte
// C2 are 8 useful, and BeforeEncrypt gets 9. In the C2 the serial overwrites the size's low byte.
TEST(EncryptedPacketTest, BeforeEncryptGetsThePaddingAlsoWhenTheBlockIsFull)
{
    const KeyPair identity = IdentityPair();
    for (const auto& [type, size] :
         {std::pair{PacketType::C1, std::size_t{9}}, std::pair{PacketType::C2, std::size_t{10}}})
    {
        PacketSerials serials = HavingSent(7);
        ByteVector seen;
        const RecordingObfuscation recording(seen);
        const ByteVector plain = PlainOfSize(type, size);

        const auto encrypted = EncryptPacket(PacketOf(plain), identity.encryption, serials, recording, std::byte{0xA5});

        ASSERT_TRUE(encrypted.HasValue()) << "un C" << (type == PacketType::C1 ? 1 : 2);
        ByteVector expected(plain.begin() + static_cast<std::ptrdiff_t>(HeaderSize(type) - 1), plain.end());
        expected[0] = std::byte{0x07};
        expected.push_back(std::byte{0xA5});
        EXPECT_TRUE(SameBytes(seen, expected)) << "un C" << (type == PacketType::C1 ? 1 : 2);
        EXPECT_EQ(AfterHeader(encrypted.Value()).size(), Client::Crypto::CipherBlockSize);
    }
}

// Re-encrypting a received packet makes another container: it does not inherit the decoder's credit.
TEST(EncryptedPacketTest, EncryptionDoesNotClaimDecryptionEvenWhenTheInputWasDecrypted)
{
    const KeyPair identity = IdentityPair();
    const ByteVector c4 = ReadNetVector("c4-serial2.bin");
    ASSERT_FALSE(c4.empty());
    for (const auto& bytes : {NoticeSerial0(), c4})
    {
        PacketSerials receiver;
        const auto decrypted = DecryptPacket(PacketOf(bytes), identity.decryption, receiver);
        ASSERT_TRUE(decrypted.HasValue());
        ASSERT_TRUE(decrypted.Value().packet.WasDecrypted());
        for (const bool wasDecrypted : {false, true})
        {
            const auto view = decrypted.Value().packet.Bytes();
            const Packet clear = PacketOf(ByteVector(view.begin(), view.end()));
            const Packet& plain = wasDecrypted ? decrypted.Value().packet : clear;
            PacketSerials sender;
            const auto encrypted = EncryptPacket(plain, identity.encryption, sender);
            ASSERT_TRUE(encrypted.HasValue());
            EXPECT_FALSE(encrypted.Value().WasDecrypted());
            EXPECT_EQ(encrypted.Value().Type(),
                      decrypted.Value().packet.Type() == PacketType::C1 ? PacketType::C3 : PacketType::C4);
            PacketSerials roundtripSerials;
            const auto roundtrip = DecryptPacket(encrypted.Value(), identity.decryption, roundtripSerials);
            ASSERT_TRUE(roundtrip.HasValue());
            EXPECT_TRUE(SameBytes(roundtrip.Value().packet.Bytes(), view));
        }
    }
}
} // namespace
