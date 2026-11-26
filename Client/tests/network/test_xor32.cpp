#include "Crypto/Xor32.h"

#include "Network/Packet.h"
#include "Network/PacketWriter.h"

#include "Network/Xor32Packet.h"
#include "support/BytePattern.h"
#include "support/Bytes.h"
#include "support/NetVectors.h"
#include "support/Packets.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <utility>
#include <vector>

// With the wire's packets (04-protocolo.md), the binary's C2 and the vectors of Client/tests/vectors/net/.
namespace
{
using Client::Crypto::Xor32DecodeBytes;
using Client::Crypto::Xor32EncodeBytes;
using Client::Crypto::Xor32Key;
using Client::Network::Packet;
using Client::Network::PacketWriter;
using Client::Network::Xor32Decode;
using Client::Network::Xor32Encode;
using Client::Tests::BytePattern;
using Client::Tests::Bytes;
using Client::Tests::PacketOf;
using Client::Tests::ReadNetVector;
using Client::Tests::SameBytes;

std::vector<std::byte> Encoded(std::vector<std::byte> bytes)
{
    const Packet packet = Xor32Encode(PacketOf(std::move(bytes)));
    return {packet.Bytes().begin(), packet.Bytes().end()};
}

// The lab wire's two C1s that 04 shows in the clear and encrypted.
TEST(Xor32Test, EncodesTheC1sOfTheWire)
{
    EXPECT_TRUE(SameBytes(Encoded(Bytes({0xC1, 0x04, 0xF3, 0x00})), Bytes({0xC1, 0x04, 0xF3, 0x7A})));
    EXPECT_TRUE(
        SameBytes(Encoded(Bytes({0xC1, 0x06, 0xA9, 0x00, 0x00, 0x01})), Bytes({0xC1, 0x06, 0xA9, 0x20, 0x9C, 0x2F})));
}

// The notice of a C3 that could not be decrypted, as 0x4e1d50 builds it before wrapping it: test_net_send.c's
// FIX_F1_PLAIN, in mu-client.
TEST(Xor32Test, EncodesTheDecryptErrorReport)
{
    EXPECT_TRUE(
        SameBytes(Encoded(Bytes({0xC1, 0x06, 0xF1, 0x03, 0x06, 0x00})), Bytes({0xC1, 0x06, 0xF1, 0x7B, 0xC1, 0x73})));
}

// A C2 shaped like the letter window's (`0x498b2c..0x498b89`, the only sender of a C2): the opcode stays in the clear
// and the first encrypted byte, 4, chains with it (03 § Envío). The bytes are the CL2.0 guide's example; the window
// sends another opcode, `C5`.
TEST(Xor32Test, EncodesAC2WithTheOpcodeInClear)
{
    EXPECT_TRUE(
        SameBytes(Encoded(Bytes({0xC2, 0x00, 0x06, 0x42, 0x0B, 0x30})), Bytes({0xC2, 0x00, 0x06, 0x42, 0xF5, 0x77})));
}

// 300 bytes, with the key wrapping nine times.
TEST(Xor32Test, EncodesTheLongC2OfTheVectors)
{
    const std::vector<std::byte> expected = ReadNetVector("c2-long.bin");
    auto plain = PacketWriter::C2(0x42).PutBytes(BytePattern(296, 37, 11)).Finish();
    ASSERT_TRUE(plain.HasValue());

    const Packet encoded = Xor32Encode(std::move(plain).Value());

    EXPECT_TRUE(SameBytes(encoded.Bytes(), expected));
}

// Each encrypted byte is the plain one with its index's key and the previous encrypted byte, and the key restarts
// every 32: byte 35 uses key[3], like byte 3, and byte 19 uses key[19].
TEST(Xor32Test, TheKeyStartsOverAtByte32)
{
    constexpr std::size_t Size = 70;
    std::vector<std::byte> plain = BytePattern(Size, 37, 11);
    plain[0] = std::byte{0xC1};
    plain[1] = std::byte{Size};

    const std::vector<std::byte> encoded = Encoded(plain);

    for (std::size_t i = 3; i < Size; ++i)
    {
        EXPECT_EQ(encoded[i] ^ plain[i] ^ encoded[i - 1], std::byte{Xor32Key[i % Xor32Key.size()]}) << i;
    }
}

TEST(Xor32Test, DecodeUndoesEncodeAndKeepsTheOpcode)
{
    for (const bool isC1 : {true, false})
    {
        PacketWriter writer = isC1 ? PacketWriter::C1(0x55) : PacketWriter::C2(0x55);
        auto plain = writer.PutBytes(BytePattern(isC1 ? 252 : 296, 37, 11)).Finish();
        ASSERT_TRUE(plain.HasValue());
        const std::vector<std::byte> before(plain.Value().Bytes().begin(), plain.Value().Bytes().end());

        const Packet encoded = Xor32Encode(std::move(plain).Value());
        EXPECT_EQ(encoded.Opcode(), std::uint8_t{0x55});
        EXPECT_FALSE(SameBytes(encoded.Bytes(), before));
        const Packet decoded = Xor32Decode(encoded);

        EXPECT_TRUE(SameBytes(decoded.Bytes(), before)) << (isC1 ? "C1" : "C2");
    }
}

// What they carry is encrypted before wrapping.
TEST(Xor32Test, LeavesC3AndC4AsTheyAre)
{
    const std::vector<std::byte> c3 = Bytes({0xC3, 0x05, 0x20, 0xAA, 0xBB});
    const std::vector<std::byte> c4 = Bytes({0xC4, 0x00, 0x05, 0x20, 0xAA});

    EXPECT_TRUE(SameBytes(Encoded(c3), c3));
    EXPECT_TRUE(SameBytes(Encoded(c4), c4));
    EXPECT_TRUE(SameBytes(Xor32Decode(PacketOf(c3)).Bytes(), c3));
    EXPECT_TRUE(SameBytes(Xor32Decode(PacketOf(c4)).Bytes(), c4));
}

// What FoxAnti encrypts in a C3: the serial and the C1 from its opcode. With (sealed, 2, 1) it equals the encrypted
// C1, and the serial and the opcode stay in the clear.
TEST(Xor32Test, EncodeBytesOnTheSerialAndTheC1FromItsOpcode)
{
    const std::vector<std::byte> c1 = Bytes({0xC1, 0x06, 0xF1, 0x03, 0x06, 0x00});
    std::vector<std::byte> sealed = Bytes({0x2A});
    sealed.insert(sealed.end(), c1.begin() + 2, c1.end());

    Xor32EncodeBytes(sealed, 2, 1);

    const std::vector<std::byte> encoded = Encoded(c1);
    EXPECT_EQ(sealed[0], std::byte{0x2A});
    EXPECT_TRUE(SameBytes(std::span(sealed).subspan(1), std::span(encoded).subspan(2)));

    Xor32DecodeBytes(sealed, 2, 1);
    EXPECT_TRUE(SameBytes(std::span(sealed).subspan(1), std::span(c1).subspan(2)));
}

// Byte 0 has no previous: with `first` at 0 it starts at 1. With `first` past the end nothing changes.
TEST(Xor32Test, EncodeBytesNeverTouchesTheFirstByte)
{
    const std::vector<std::byte> plain = BytePattern(8, 37, 11);
    std::vector<std::byte> fromZero = plain;
    std::vector<std::byte> fromOne = plain;
    std::vector<std::byte> pastTheEnd = plain;

    Xor32EncodeBytes(fromZero, 0, 0);
    Xor32EncodeBytes(fromOne, 1, 0);
    Xor32EncodeBytes(pastTheEnd, 9, 0);

    EXPECT_EQ(fromZero[0], plain[0]);
    EXPECT_TRUE(SameBytes(fromZero, fromOne));
    EXPECT_TRUE(SameBytes(pastTheEnd, plain));
    Xor32DecodeBytes(fromZero, 0, 0);
    EXPECT_TRUE(SameBytes(fromZero, plain));
}
} // namespace
