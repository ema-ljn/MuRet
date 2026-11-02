#include "Network/Crypto/Xor32.h"

#include "Network/Packet.h"
#include "Network/PacketWriter.h"

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

// Client::Network::Crypto::Xor32: el cifrado encadenado del motor de envío, con los paquetes del
// cable (04-protocolo.md), el C2 del binario y los vectores de Client/tests/vectors/net/.
namespace
{
using Client::Network::Packet;
using Client::Network::PacketWriter;
using Client::Network::Crypto::Xor32Decode;
using Client::Network::Crypto::Xor32DecodeBytes;
using Client::Network::Crypto::Xor32Encode;
using Client::Network::Crypto::Xor32EncodeBytes;
using Client::Network::Crypto::Xor32Key;
using Client::Tests::BytePattern;
using Client::Tests::Bytes;
using Client::Tests::PacketOf;
using Client::Tests::ReadNetVector;
using Client::Tests::SameBytes;

// Los bytes de `packet` después del XOR32.
std::vector<std::byte> Encoded(std::vector<std::byte> bytes)
{
    const Packet packet = Xor32Encode(PacketOf(std::move(bytes)));
    return {packet.Bytes().begin(), packet.Bytes().end()};
}

// Los dos C1 del cable del laboratorio que el 04 muestra en claro y cifrados.
TEST(Xor32Test, EncodesTheC1sOfTheWire)
{
    EXPECT_TRUE(SameBytes(Encoded(Bytes({0xC1, 0x04, 0xF3, 0x00})), Bytes({0xC1, 0x04, 0xF3, 0x7A})));
    EXPECT_TRUE(
        SameBytes(Encoded(Bytes({0xC1, 0x06, 0xA9, 0x00, 0x00, 0x01})), Bytes({0xC1, 0x06, 0xA9, 0x20, 0x9C, 0x2F})));
}

// El aviso de un C3 que no se pudo descifrar, como lo arma 0x4e1d50 antes de envolverlo (CL2.3):
// el FIX_F1_PLAIN de test_net_send.c, en mu-client.
TEST(Xor32Test, EncodesTheDecryptErrorReport)
{
    EXPECT_TRUE(
        SameBytes(Encoded(Bytes({0xC1, 0x06, 0xF1, 0x03, 0x06, 0x00})), Bytes({0xC1, 0x06, 0xF1, 0x7B, 0xC1, 0x73})));
}

// Un C2 con el modelo de la ventana de cartas (`0x498b2c..0x498b89`, el único emisor de un C2): el
// opcode va en claro, y el primer byte cifrado, el 4, se encadena con él (03 § Envío). Los bytes
// son los del ejemplo de la guía de la CL2.0; la ventana manda otro opcode, `C5`.
TEST(Xor32Test, EncodesAC2WithTheOpcodeInClear)
{
    EXPECT_TRUE(
        SameBytes(Encoded(Bytes({0xC2, 0x00, 0x06, 0x42, 0x0B, 0x30})), Bytes({0xC2, 0x00, 0x06, 0x42, 0xF5, 0x77})));
}

// El C2 largo de los vectores: 300 bytes, con la clave dando la vuelta nueve veces.
TEST(Xor32Test, EncodesTheLongC2OfTheVectors)
{
    const std::vector<std::byte> expected = ReadNetVector("c2-long.bin");
    auto plain = PacketWriter::C2(0x42).PutBytes(BytePattern(296, 37, 11)).Finish();
    ASSERT_TRUE(plain.HasValue());

    const Packet encoded = Xor32Encode(std::move(plain).Value());

    EXPECT_TRUE(SameBytes(encoded.Bytes(), expected));
}

// Cada byte cifrado es el claro con la clave de su índice y el cifrado anterior, y la clave vuelve a
// empezar cada 32: el 35 usa key[3], como el 3, y el 19 usa key[19].
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

// Descifrar deshace cifrar, en un C1 y en un C2, y el opcode no cambia en ninguno.
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

// Un C3 y un C4 vuelven igual: lo que llevan adentro se cifra antes de envolverlo.
TEST(Xor32Test, LeavesC3AndC4AsTheyAre)
{
    const std::vector<std::byte> c3 = Bytes({0xC3, 0x05, 0x20, 0xAA, 0xBB});
    const std::vector<std::byte> c4 = Bytes({0xC4, 0x00, 0x05, 0x20, 0xAA});

    EXPECT_TRUE(SameBytes(Encoded(c3), c3));
    EXPECT_TRUE(SameBytes(Encoded(c4), c4));
    EXPECT_TRUE(SameBytes(Xor32Decode(PacketOf(c3)).Bytes(), c3));
    EXPECT_TRUE(SameBytes(Xor32Decode(PacketOf(c4)).Bytes(), c4));
}

// Lo que FoxAnti cifra en un C3 (CL2.4): el serial y el C1 desde su opcode. Con (sealed, 2, 1) da lo
// mismo que el C1 cifrado, y el serial y el opcode quedan en claro.
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

// El byte 0 no tiene anterior: con `first` en 0 empieza en 1. Con `first` después del final no
// cambia nada.
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
