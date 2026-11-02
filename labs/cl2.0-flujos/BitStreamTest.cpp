// Labs::BitWriter y Labs::BitReader: bits MSB primero, el bloque de 11 bytes de SimpleModulus, y el
// orden en que el original escribe un carril de 18 bits.

#include "BitStream.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

namespace
{

using Labs::BitReader;
using Labs::BitWriter;

std::vector<std::byte> Bytes(std::initializer_list<int> values)
{
    std::vector<std::byte> bytes;
    for (const int value : values)
    {
        bytes.push_back(static_cast<std::byte>(value));
    }
    return bytes;
}

std::vector<std::byte> Written(const BitWriter& writer)
{
    const std::span<const std::byte> bytes = writer.Bytes();
    return {bytes.begin(), bytes.end()};
}

// Un carril como lo escribe el original: sus dos bytes de abajo, de a 8 bits empezando por el de
// abajo, y después los 2 bits que le quedan arriba (el 17 y el 16) como un campo de 2 bits. En un
// carril cifrado entran 18 bits porque el módulo tiene hasta 18. Lo fija el contrato de la CL2.3
// (plan de la CL2, "bits de un carril"): 0x685bb0 llama a AddBits con 2 bits desde la posición 22
// del dword, y es lo que hacen encrypt_block y decrypt_block de c3c4_reference.py.
void WriteLaneLikeTheOriginal(BitWriter& writer, std::uint32_t lane)
{
    // Bits de un byte, y los 2 que le quedan arriba a un carril de 18 bits.
    constexpr std::size_t ByteBits = 8;
    constexpr std::size_t TopBits = 2;

    writer.Write(lane & 0xFF, ByteBits);
    writer.Write((lane >> 8) & 0xFF, ByteBits);
    writer.Write(lane >> 16, TopBits);
}

// El primer bit del flujo es el 0x80 del byte 0.
TEST(BitWriterTest, FirstBitIsTheMsbOfByteZero)
{
    BitWriter writer;

    writer.Write(1, 1);

    EXPECT_EQ(Written(writer), Bytes({0x80}));
    EXPECT_EQ(writer.BitCount(), 1u);
}

// Un campo sigue donde terminó el anterior, aunque sea en la mitad de un byte: 101 y 111111 son
// 1011 1111 y un 1 en el byte siguiente.
TEST(BitWriterTest, FieldCrossesAByteBoundary)
{
    BitWriter writer;

    writer.Write(0b101, 3);
    writer.Write(0b111111, 6);

    EXPECT_EQ(Written(writer), Bytes({0xBF, 0x80}));
    EXPECT_EQ(writer.BitCount(), 9u);
}

// Solo se escriben los bitCount bits de abajo: de 0xFF con 4 bits sale 1111.
TEST(BitWriterTest, IgnoresBitsAboveTheField)
{
    BitWriter writer;

    writer.Write(0xFF, 4);

    EXPECT_EQ(Written(writer), Bytes({0xF0}));
}

// Un campo más ancho que el valor lleva ceros a la izquierda.
TEST(BitWriterTest, ZeroExtendsAFieldWiderThan32Bits)
{
    BitWriter writer;

    writer.Write(1, 40);

    EXPECT_EQ(Written(writer), Bytes({0x00, 0x00, 0x00, 0x00, 0x01}));
}

// El bloque de SimpleModulus: cuatro carriles de 18 bits y dos bytes de cola son 88 bits, justo 11
// bytes, y se leen iguales.
TEST(BitWriterTest, FourEighteenBitFieldsAndASixteenBitTailFillElevenBytes)
{
    // Bits de un carril cifrado y de la cola, y lo que mide el bloque cifrado.
    constexpr std::size_t LaneBits = 18;
    constexpr std::size_t TailBits = 16;
    constexpr std::size_t BlockBytes = 11;
    constexpr std::array<std::uint32_t, 4> Lanes = {0x3FFFF, 0x00001, 0x2AAAA, 0x15555};
    constexpr std::uint32_t Tail = 0xBEEF;

    BitWriter writer;
    for (const std::uint32_t lane : Lanes)
    {
        writer.Write(lane, LaneBits);
    }
    writer.Write(Tail, TailBits);

    EXPECT_EQ(writer.BitCount(), 88u);
    EXPECT_EQ(Written(writer), Bytes({0xFF, 0xFF, 0xC0, 0x00, 0x1A, 0xAA, 0xA9, 0x55, 0x55, 0xBE, 0xEF}));
    ASSERT_EQ(writer.Bytes().size(), BlockBytes);

    BitReader reader(writer.Bytes());
    for (const std::uint32_t lane : Lanes)
    {
        EXPECT_EQ(reader.Read(LaneBits), lane);
    }
    EXPECT_EQ(reader.Read(TailBits), Tail);
    EXPECT_EQ(reader.RemainingBits(), 0u);
}

// El orden del original: 0x10000 tiene prendido solo el bit 16, que va último, y da 00 00 40;
// 0x20000, solo el 17, que va anteúltimo, y da 00 00 80.
TEST(BitWriterTest, WritesALaneLikeTheOriginal)
{
    BitWriter bit16;
    WriteLaneLikeTheOriginal(bit16, 0x10000);
    EXPECT_EQ(Written(bit16), Bytes({0x00, 0x00, 0x40}));

    BitWriter bit17;
    WriteLaneLikeTheOriginal(bit17, 0x20000);
    EXPECT_EQ(Written(bit17), Bytes({0x00, 0x00, 0x80}));

    BitWriter mixed;
    WriteLaneLikeTheOriginal(mixed, 0x2ABCD);
    EXPECT_EQ(Written(mixed), Bytes({0xCD, 0xAB, 0x80}));
}

// Un carril del original no es un campo de 18 bits MSB primero: el mismo 0x10000 da 40 00 00. Un
// round-trip no lo nota, porque el que lee con el mismo orden recupera el valor igual; solo un
// vector fijo, con los bits 16 y 17 prendidos, dice cuál es el orden del original.
TEST(BitWriterTest, ALaneIsNotAnEighteenBitField)
{
    // Bits de un carril cifrado.
    constexpr std::size_t LaneBits = 18;

    BitWriter field;
    field.Write(0x10000, LaneBits);
    EXPECT_EQ(Written(field), Bytes({0x40, 0x00, 0x00}));

    BitReader roundTrip(field.Bytes());
    EXPECT_EQ(roundTrip.Read(LaneBits), 0x10000u);
}

// Leer va en el mismo orden: 0x80 es un 1 y siete 0.
TEST(BitReaderTest, ReadsMsbFirst)
{
    const std::vector<std::byte> bytes = Bytes({0x80});
    BitReader reader(bytes);

    EXPECT_EQ(reader.Read(1), 1u);
    EXPECT_EQ(reader.Read(7), 0u);
    EXPECT_EQ(reader.RemainingBits(), 0u);
}

// Una lectura que se pasa del final falla y no avanza: lo que quedaba se puede leer después.
TEST(BitReaderTest, FailsPastTheEndWithoutMoving)
{
    const std::vector<std::byte> bytes = Bytes({0xAB});
    BitReader reader(bytes);

    EXPECT_EQ(reader.Read(9), std::nullopt);
    EXPECT_EQ(reader.RemainingBits(), 8u);
    EXPECT_EQ(reader.Read(8), 0xABu);
}

// Más de 32 bits no entran en el resultado: falla aunque los bytes alcancen.
TEST(BitReaderTest, RejectsMoreThan32Bits)
{
    const std::vector<std::byte> bytes = Bytes({0x01, 0x02, 0x03, 0x04, 0x05});
    BitReader reader(bytes);

    EXPECT_EQ(reader.Read(33), std::nullopt);
    EXPECT_EQ(reader.RemainingBits(), 40u);
    EXPECT_EQ(reader.Read(32), 0x01020304u);
}

// El bit 31, el más alto que entra: se escribe y se lee como cualquier otro.
TEST(BitReaderTest, WritesAndReadsBit31)
{
    BitWriter writer;
    writer.Write(0x80000001, 32);

    const std::vector<std::byte> bytes = Written(writer);
    EXPECT_EQ(bytes, Bytes({0x80, 0x00, 0x00, 0x01}));
    BitReader reader(bytes);
    EXPECT_EQ(reader.Read(32), 0x80000001u);
}

// Escribir 0 bits no escribe nada.
TEST(BitWriterTest, WritingZeroBitsWritesNothing)
{
    BitWriter writer;
    writer.Write(5, 0);

    EXPECT_TRUE(writer.Bytes().empty());
    EXPECT_EQ(writer.BitCount(), 0u);
}

// Leer 0 bits no mueve nada y da 0.
TEST(BitReaderTest, ReadingZeroBitsGivesZero)
{
    const std::vector<std::byte> bytes = Bytes({0xFF});
    BitReader reader(bytes);

    EXPECT_EQ(reader.Read(0), 0u);
    EXPECT_EQ(reader.RemainingBits(), 8u);
}

} // namespace
