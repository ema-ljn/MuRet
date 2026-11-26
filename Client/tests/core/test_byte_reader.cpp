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

// Built over bytes that stay alive, not over a temporary that owns its own.
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

// F1 E2 00 00 is Item.bmd's checksum key, 0xE2F1: the least significant byte goes first.
TEST(ByteReaderTest, ReadsU32LittleEndian)
{
    const auto bytes = Bytes(0xF1, 0xE2, 0x00, 0x00);
    ByteReader reader(bytes);

    EXPECT_EQ(reader.ReadU32Le(), 0xE2F1u);
    EXPECT_EQ(reader.Remaining(), 0u);
}

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

// No sign extension: 0x80 is 128, not -128, and 0xFF is 255, not -1. Extended, the high bytes would fill with ones:
// 0xFFFFFF80 and 0xFFFF.
TEST(ByteReaderTest, HighBitStaysUnsigned)
{
    const auto bytes = Bytes(0x80, 0x00, 0x00, 0x00, 0xFF, 0x00);
    ByteReader reader(bytes);

    EXPECT_EQ(reader.ReadU32Le(), 0x80u);
    EXPECT_EQ(reader.ReadU16Le(), std::uint16_t{0x00FF});
}

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

// A C2/C4 size is big-endian, most significant byte first (03, "Cadena de recepción"): 01 2C is 300, not 0x2C01.
TEST(ByteReaderTest, ReadsU16BigEndian)
{
    const auto bytes = Bytes(0x01, 0x2C);
    ByteReader reader(bytes);

    EXPECT_EQ(reader.ReadU16Be(), std::uint16_t{0x012C});
    EXPECT_EQ(reader.Remaining(), 0u);
}

TEST(ByteReaderTest, MixesBigAndLittleEndian)
{
    const auto bytes = Bytes(0x12, 0x34, 0x56, 0x78);
    ByteReader reader(bytes);

    EXPECT_EQ(reader.ReadU16Be(), std::uint16_t{0x1234});
    EXPECT_EQ(reader.ReadU16Le(), std::uint16_t{0x7856});
    EXPECT_EQ(reader.Remaining(), 0u);
}

// 00 FE is the longest C2 on the lab's wire, 254 bytes, and FF 80 is 0xFF80, not a negative.
TEST(ByteReaderTest, HighBitStaysUnsignedInBigEndian)
{
    const auto bytes = Bytes(0x00, 0xFE, 0xFF, 0x80);
    ByteReader reader(bytes);

    EXPECT_EQ(reader.ReadU16Be(), std::uint16_t{0x00FE});
    EXPECT_EQ(reader.ReadU16Be(), std::uint16_t{0xFF80});
}

TEST(ByteReaderTest, ReadU16BigEndianPastTheEndFails)
{
    const auto bytes = Bytes(0xC2);
    ByteReader reader(bytes);

    EXPECT_EQ(reader.ReadU16Be(), std::nullopt);
    EXPECT_EQ(reader.Remaining(), 1u);
}

// Like the 16-byte host of an F4 03 (04, "El flujo de conexión").
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

// A 0-byte field always fits, empty.
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
