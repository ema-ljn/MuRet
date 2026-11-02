// Client::Network::PacketWriter: un paquete C1 o C2 campo por campo, sin XOR32 (eso es Xor32Test).

#include "Network/PacketWriter.h"

#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace
{

using Client::Network::MaxC1Size;
using Client::Network::MaxWrittenPacketSize;
using Client::Network::PacketError;
using Client::Network::PacketType;
using Client::Network::PacketWriter;
using Client::Tests::SameBytes;

std::vector<std::byte> Bytes(std::initializer_list<int> values)
{
    std::vector<std::byte> bytes;
    for (const int value : values)
    {
        bytes.push_back(static_cast<std::byte>(value));
    }
    return bytes;
}

// Un escritor del tipo `type` que al terminar mide `size` bytes: la cabecera, el opcode 0x42 y el
// resto en 0x11.
PacketWriter OfSize(PacketType type, std::size_t size)
{
    PacketWriter writer = type == PacketType::C1 ? PacketWriter::C1(0x42) : PacketWriter::C2(0x42);
    const std::size_t opcodeEnd = type == PacketType::C1 ? 3 : 4;
    const std::vector<std::byte> rest(size - opcodeEnd, std::byte{0x11});
    writer.PutBytes(rest);
    return writer;
}

// El primer paquete del cable del 04, antes del XOR32: C1, el tamaño, el opcode y un campo.
TEST(PacketWriterTest, BuildsAC1WithItsSize)
{
    const auto packet = PacketWriter::C1(0xF3).PutU8(0x00).Finish();

    ASSERT_TRUE(packet.HasValue());
    EXPECT_EQ(packet.Value().Type(), PacketType::C1);
    EXPECT_EQ(packet.Value().Opcode(), std::uint8_t{0xF3});
    EXPECT_TRUE(SameBytes(packet.Value().Bytes(), Bytes({0xC1, 0x04, 0xF3, 0x00})));
}

// El C2 de la ventana de cartas, antes del XOR32: el tamaño en dos bytes, big-endian.
TEST(PacketWriterTest, BuildsAC2WithItsSizeBigEndian)
{
    const auto packet = PacketWriter::C2(0x42).PutU8(0x0B).PutU8(0x30).Finish();

    ASSERT_TRUE(packet.HasValue());
    EXPECT_EQ(packet.Value().Type(), PacketType::C2);
    EXPECT_TRUE(SameBytes(packet.Value().Bytes(), Bytes({0xC2, 0x00, 0x06, 0x42, 0x0B, 0x30})));
}

// Los enteros van little-endian, salvo PutU16Be, en el orden en que se agregan.
TEST(PacketWriterTest, PutsEachFieldInOrder)
{
    const std::vector<std::byte> tail = Bytes({0xAA, 0xBB});

    const auto packet =
        PacketWriter::C1(0x10).PutU16Le(0x1234).PutU16Be(0x1234).PutU32Le(0x11223344).PutBytes(tail).Finish();

    ASSERT_TRUE(packet.HasValue());
    EXPECT_TRUE(SameBytes(packet.Value().Bytes(),
                          Bytes({0xC1, 0x0D, 0x10, 0x34, 0x12, 0x12, 0x34, 0x44, 0x33, 0x22, 0x11, 0xAA, 0xBB})));
}

// El tamaño de un C2, con el byte alto en 0 y distinto de 0.
TEST(PacketWriterTest, WritesTheSizeOfAC2BigEndian)
{
    const struct
    {
        std::size_t size;
        int high;
        int low;
    } cases[] = {{255, 0x00, 0xFF}, {256, 0x01, 0x00}, {300, 0x01, 0x2C}, {MaxWrittenPacketSize, 0x08, 0x00}};

    for (const auto& expected : cases)
    {
        const auto packet = OfSize(PacketType::C2, expected.size).Finish();

        ASSERT_TRUE(packet.HasValue()) << expected.size;
        const auto bytes = packet.Value().Bytes();
        ASSERT_EQ(bytes.size(), expected.size);
        EXPECT_EQ(bytes[1], std::byte(expected.high)) << expected.size;
        EXPECT_EQ(bytes[2], std::byte(expected.low)) << expected.size;
    }
}

// Q-C06: el original deja de agregar con 0x800 bytes, y un C1 de más de 255 sale con el tamaño
// truncado. MuRet no saca ninguno de los dos: es TooLarge.
TEST(PacketWriterTest, RejectsWhatDoesNotFitInsteadOfTruncatingIt)
{
    EXPECT_TRUE(OfSize(PacketType::C1, MaxC1Size).Finish().HasValue());
    const auto c1 = OfSize(PacketType::C1, MaxC1Size + 1).Finish();
    ASSERT_FALSE(c1.HasValue());
    EXPECT_EQ(c1.Error(), PacketError::TooLarge);

    EXPECT_TRUE(OfSize(PacketType::C2, MaxWrittenPacketSize).Finish().HasValue());
    const auto c2 = OfSize(PacketType::C2, MaxWrittenPacketSize + 1).Finish();
    ASSERT_FALSE(c2.HasValue());
    EXPECT_EQ(c2.Error(), PacketError::TooLarge);
}

// Finish() no cambia el escritor: se puede terminar dos veces, o seguir agregando.
TEST(PacketWriterTest, FinishLeavesTheWriterAsItWas)
{
    PacketWriter writer = PacketWriter::C1(0x20);
    writer.PutU8(0xAA);

    const auto first = writer.Finish();
    const auto again = writer.Finish();
    writer.PutU8(0xBB);
    const auto longer = writer.Finish();

    ASSERT_TRUE(first.HasValue() && again.HasValue() && longer.HasValue());
    EXPECT_TRUE(SameBytes(again.Value().Bytes(), first.Value().Bytes()));
    EXPECT_TRUE(SameBytes(longer.Value().Bytes(), Bytes({0xC1, 0x05, 0x20, 0xAA, 0xBB})));
}

// Un paquete sin campos: la cabecera y el opcode.
TEST(PacketWriterTest, APacketWithoutFieldsIsHeaderAndOpcode)
{
    const auto c1 = PacketWriter::C1(0x0E).Finish();
    const auto c2 = PacketWriter::C2(0x0E).Finish();

    ASSERT_TRUE(c1.HasValue() && c2.HasValue());
    EXPECT_TRUE(SameBytes(c1.Value().Bytes(), Bytes({0xC1, 0x03, 0x0E})));
    EXPECT_TRUE(SameBytes(c2.Value().Bytes(), Bytes({0xC2, 0x00, 0x04, 0x0E})));
}

} // namespace
