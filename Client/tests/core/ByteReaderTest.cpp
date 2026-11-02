// Client::Core::ByteReader: enteros little-endian y big-endian, y campos de bytes, leídos en orden,
// sin pasarse del final.

#include "Core/ByteReader.h"

#include <gtest/gtest.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace
{

using Client::Core::ByteReader;

// Se arma sobre bytes que siguen vivos, y no sobre un temporal que es dueño de los suyos.
static_assert(std::constructible_from<ByteReader, std::span<const std::byte>>);
static_assert(std::constructible_from<ByteReader, std::vector<std::byte>&>);
static_assert(std::constructible_from<ByteReader, const std::vector<std::byte>&>);
static_assert(!std::constructible_from<ByteReader, std::vector<std::byte>>);
static_assert(!std::constructible_from<ByteReader, std::array<std::byte, 4>>);
static_assert(std::copy_constructible<ByteReader>);

template <typename... Values> constexpr std::array<std::byte, sizeof...(Values)> Bytes(Values... values)
{
    return {static_cast<std::byte>(values)...};
}

// F1 E2 00 00 es la clave del checksum de Item.bmd, 0xE2F1: el byte menos significativo va primero.
TEST(ByteReaderTest, ReadsU32LittleEndian)
{
    const auto bytes = Bytes(0xF1, 0xE2, 0x00, 0x00);
    ByteReader reader(bytes);

    EXPECT_EQ(reader.ReadU32Le(), 0xE2F1u);
    EXPECT_EQ(reader.Remaining(), 0u);
}

// Cada lectura avanza lo que leyó, ni más ni menos: un u8 con bytes de sobra avanza uno solo.
TEST(ByteReaderTest, ReadsInOrder)
{
    const auto bytes = Bytes(0x07, 0x18, 0x5A, 0x09);
    ByteReader reader(bytes);

    EXPECT_EQ(reader.ReadU8(), std::uint8_t{0x07});
    EXPECT_EQ(reader.Remaining(), 3u);
    EXPECT_EQ(reader.ReadU16Le(), std::uint16_t{0x5A18});
    EXPECT_EQ(reader.Remaining(), 1u);
    EXPECT_EQ(reader.ReadU8(), std::uint8_t{0x09});
    EXPECT_EQ(reader.Remaining(), 0u);
}

// Con el bit alto prendido no hay signo que se extienda: 0x80 es 128 y no -128, y 0xFF es 255 y
// no -1. Si se extendiera, los bytes altos se llenarían de unos: 0xFFFFFF80 y 0xFFFF.
TEST(ByteReaderTest, HighBitStaysUnsigned)
{
    const auto bytes = Bytes(0x80, 0x00, 0x00, 0x00, 0xFF, 0x00);
    ByteReader reader(bytes);

    EXPECT_EQ(reader.ReadU32Le(), 0x80u);
    EXPECT_EQ(reader.ReadU16Le(), std::uint16_t{0x00FF});
}

// Una lectura que no entra falla sin leer nada ni avanzar: la que sigue arranca en el mismo lugar.
TEST(ByteReaderTest, ReadPastTheEndFailsAndKeepsThePosition)
{
    const auto bytes = Bytes(0x01, 0x02, 0x03);
    ByteReader reader(bytes);

    EXPECT_EQ(reader.ReadU32Le(), std::nullopt);
    EXPECT_EQ(reader.Remaining(), 3u);
    EXPECT_EQ(reader.ReadU16Le(), std::uint16_t{0x0201});
    EXPECT_EQ(reader.ReadU16Le(), std::nullopt);
    EXPECT_EQ(reader.ReadU8(), std::uint8_t{0x03});
    EXPECT_EQ(reader.ReadU8(), std::nullopt);
}

// El tamaño de un C2/C4 va en big-endian, el byte más significativo primero (03, "Cadena de
// recepción"): 01 2C son 300, no 0x2C01.
TEST(ByteReaderTest, ReadsU16BigEndian)
{
    const auto bytes = Bytes(0x01, 0x2C);
    ByteReader reader(bytes);

    EXPECT_EQ(reader.ReadU16Be(), std::uint16_t{0x012C});
    EXPECT_EQ(reader.Remaining(), 0u);
}

// Big-endian y little-endian se mezclan en el mismo buffer, y cada lectura sigue donde terminó la
// anterior.
TEST(ByteReaderTest, MixesBigAndLittleEndian)
{
    const auto bytes = Bytes(0x12, 0x34, 0x56, 0x78);
    ByteReader reader(bytes);

    EXPECT_EQ(reader.ReadU16Be(), std::uint16_t{0x1234});
    EXPECT_EQ(reader.ReadU16Le(), std::uint16_t{0x7856});
    EXPECT_EQ(reader.Remaining(), 0u);
}

// En big-endian el bit alto tampoco extiende el signo: 00 FE es el C2 más largo del cable del
// laboratorio, 254 bytes, y FF 80 es 0xFF80, no un negativo.
TEST(ByteReaderTest, HighBitStaysUnsignedInBigEndian)
{
    const auto bytes = Bytes(0x00, 0xFE, 0xFF, 0x80);
    ByteReader reader(bytes);

    EXPECT_EQ(reader.ReadU16Be(), std::uint16_t{0x00FE});
    EXPECT_EQ(reader.ReadU16Be(), std::uint16_t{0xFF80});
}

// Con un solo byte, un u16 big-endian falla sin avanzar, como los little-endian.
TEST(ByteReaderTest, ReadU16BigEndianPastTheEndFails)
{
    const auto bytes = Bytes(0xC2);
    ByteReader reader(bytes);

    EXPECT_EQ(reader.ReadU16Be(), std::nullopt);
    EXPECT_EQ(reader.Remaining(), 1u);
}

// Un campo de largo fijo, como el host de 16 bytes de un F4 03 (04, "El flujo de conexión"): sale
// sin copiarse, mirando el mismo buffer, y la lectura que sigue arranca después.
TEST(ByteReaderTest, ReadsBytesInOrderWithoutCopying)
{
    const auto bytes = Bytes(0x31, 0x37, 0x32, 0x00, 0x5D, 0xDA);
    ByteReader reader(bytes);

    const std::optional<std::span<const std::byte>> field = reader.ReadBytes(4);

    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->data(), bytes.data());
    EXPECT_EQ(field->size(), 4u);
    EXPECT_EQ(reader.ReadU16Le(), std::uint16_t{0xDA5D});
    EXPECT_EQ(reader.Remaining(), 0u);
}

// Un campo que no entra falla sin avanzar, como un entero; uno de 0 bytes entra siempre, vacío.
TEST(ByteReaderTest, ReadBytesPastTheEndFailsAndKeepsThePosition)
{
    const auto bytes = Bytes(0x01, 0x02, 0x03);
    ByteReader reader(bytes);

    EXPECT_EQ(reader.ReadBytes(4), std::nullopt);
    EXPECT_EQ(reader.Remaining(), 3u);
    const std::optional<std::span<const std::byte>> all = reader.ReadBytes(3);
    ASSERT_TRUE(all.has_value());
    EXPECT_EQ(all->size(), 3u);
    const std::optional<std::span<const std::byte>> empty = reader.ReadBytes(0);
    ASSERT_TRUE(empty.has_value());
    EXPECT_TRUE(empty->empty());
    EXPECT_EQ(reader.ReadBytes(1), std::nullopt);
}

TEST(ByteReaderTest, EmptyBufferHasNothingToRead)
{
    ByteReader reader(std::span<const std::byte>{});

    EXPECT_EQ(reader.Remaining(), 0u);
    EXPECT_EQ(reader.ReadU8(), std::nullopt);
    EXPECT_EQ(reader.Remaining(), 0u);
}

} // namespace
