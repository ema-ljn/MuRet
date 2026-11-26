#include "Data/Terrain/TerrainAttributes.h"

#include "Data/DataError.h"
#include "Data/Terrain/TerrainGrid.h"
#include "support/TerrainFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace
{
using Client::Data::DataError;
using Client::Data::Terrain::CellIndex;
using Client::Data::Terrain::LoadTerrainAttributes;
using Client::Data::Terrain::TerrainAttributes;
using Client::Data::Terrain::TerrainAttributesHeaderSize;
using Client::Data::Terrain::TerrainAttributesSizeWithByteCells;
using Client::Data::Terrain::TerrainAttributesSizeWithWordCells;
using Client::Data::Terrain::TerrainCellCount;
using Client::Tests::AttributeCells;
using Client::Tests::AttributePattern;
using Client::Tests::EncryptTerrainAttributes;
using Client::Tests::PlainTerrainAttributes;

constexpr std::uint8_t Devias = 3;

void ExpectThePattern(const TerrainAttributes& attributes)
{
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        ASSERT_EQ(attributes.cells[cell], AttributePattern(cell)) << "celda " << cell;
    }
}

std::optional<DataError> LoadError(const std::vector<std::byte>& plain)
{
    const auto loaded = LoadTerrainAttributes(EncryptTerrainAttributes(plain), Devias);
    if (loaded.HasValue())
    {
        return std::nullopt;
    }
    return loaded.Error();
}

// The research gives the real EncTerrain1.att's encrypted header, 8B E5 24 92, for 00 01 FF FF
// (005f8390-encterrain-att.md): XOR3 first and K16 on top.
TEST(TerrainAttributesTest, TheHeaderEncryptsLikeTheRealFile)
{
    const std::vector<std::byte> encrypted = EncryptTerrainAttributes(PlainTerrainAttributes(1, AttributeCells::Bytes));

    ASSERT_GE(encrypted.size(), 4u);
    EXPECT_EQ(std::vector(encrypted.begin(), encrypted.begin() + 4),
              (std::vector{std::byte{0x8B}, std::byte{0xE5}, std::byte{0x24}, std::byte{0x92}}));
}

TEST(TerrainAttributesTest, ReadsOneBytePerCell)
{
    const auto loaded =
        LoadTerrainAttributes(EncryptTerrainAttributes(PlainTerrainAttributes(Devias, AttributeCells::Bytes)), Devias);

    ASSERT_TRUE(loaded.HasValue());
    ExpectThePattern(*loaded.Value());
}

// Even when the high one is not zero.
TEST(TerrainAttributesTest, KeepsTheLowByteOfEachWord)
{
    const auto loaded =
        LoadTerrainAttributes(EncryptTerrainAttributes(PlainTerrainAttributes(Devias, AttributeCells::Words)), Devias);

    ASSERT_TRUE(loaded.HasValue());
    ExpectThePattern(*loaded.Value());
}

// Cell (x, y) is at y * 256 + x after the header, with WORDs too.
TEST(TerrainAttributesTest, CellXYIsRowByRow)
{
    constexpr std::uint8_t x = 3;
    constexpr std::uint8_t y = 200;
    std::vector<std::byte> plain = PlainTerrainAttributes(Devias, AttributeCells::Words);
    plain[TerrainAttributesHeaderSize + 2 * (std::size_t{y} * 256 + std::size_t{x})] = std::byte{0x7F};

    const auto loaded = LoadTerrainAttributes(EncryptTerrainAttributes(plain), Devias);

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value()->cells[CellIndex(x, y)], 0x7F);
}

// Q-C25: the original returns -1 silently and the world keeps the previous one's attributes.
TEST(TerrainAttributesTest, AnyOtherSizeIsAnError)
{
    for (const std::size_t size : {std::size_t{0}, TerrainAttributesHeaderSize, TerrainAttributesSizeWithByteCells - 1,
                                   TerrainAttributesSizeWithByteCells + 1, TerrainAttributesSizeWithWordCells - 1,
                                   TerrainAttributesSizeWithWordCells + 1})
    {
        const auto loaded = LoadTerrainAttributes(std::vector<std::byte>(size), Devias);

        ASSERT_FALSE(loaded.HasValue()) << size << " bytes";
        EXPECT_EQ(loaded.Error(), DataError::UnexpectedSize) << size << " bytes";
    }
}

// 00 N FF FF: any other byte at 0, 2 or 3 is an error.
TEST(TerrainAttributesTest, TheHeaderIs00WorldFFFF)
{
    for (const std::size_t offset : {std::size_t{0}, std::size_t{2}, std::size_t{3}})
    {
        std::vector<std::byte> plain = PlainTerrainAttributes(Devias, AttributeCells::Bytes);
        plain[offset] ^= std::byte{0x01};

        EXPECT_EQ(LoadError(plain), DataError::InvalidData) << "byte " << offset;
    }
}

// 0x7F is the highest the original accepts; 0x80 is an error, in any cell. In a WORD the low byte counts: a high
// 0x80 is dropped like any other.
TEST(TerrainAttributesTest, AnAttributeOf0x80IsAnError)
{
    const std::size_t last = TerrainCellCount - 1;
    std::vector<std::byte> bytes = PlainTerrainAttributes(Devias, AttributeCells::Bytes);
    bytes[TerrainAttributesHeaderSize + last] = std::byte{0x80};
    std::vector<std::byte> words = PlainTerrainAttributes(Devias, AttributeCells::Words);
    words[TerrainAttributesHeaderSize + 2 * last] = std::byte{0x80};
    std::vector<std::byte> highByte = PlainTerrainAttributes(Devias, AttributeCells::Words);
    highByte[TerrainAttributesHeaderSize + 2 * last + 1] = std::byte{0x80};

    EXPECT_EQ(LoadError(bytes), DataError::InvalidData);
    EXPECT_EQ(LoadError(words), DataError::InvalidData);
    EXPECT_EQ(LoadError(highByte), std::nullopt);
}

TEST(TerrainAttributesTest, AnotherWorldIsAnError)
{
    EXPECT_EQ(LoadError(PlainTerrainAttributes(2, AttributeCells::Bytes)), DataError::WrongWorld);
}

// In the original the loader shows its "Data error" and returns -1, and the caller only checks the world of a file
// that did not give -1 (0x61a759).
TEST(TerrainAttributesTest, InvalidDataComesBeforeAnotherWorld)
{
    std::vector<std::byte> badHeader = PlainTerrainAttributes(2, AttributeCells::Bytes);
    badHeader[3] ^= std::byte{0x01};
    std::vector<std::byte> badCell = PlainTerrainAttributes(2, AttributeCells::Bytes);
    badCell[TerrainAttributesHeaderSize] = std::byte{0x80};

    EXPECT_EQ(LoadError(badHeader), DataError::InvalidData);
    EXPECT_EQ(LoadError(badCell), DataError::InvalidData);
}

// Q-C26: the original expects a 5 in World1's cell 0x7B87 (005f8390-encterrain-att.md, "Centinelas") and otherwise
// it is a "Data error". MuRet does not check it.
TEST(TerrainAttributesTest, TheWitnessCellIsNotChecked)
{
    constexpr std::size_t WitnessCellOfWorld1 = 0x7B87;
    std::vector<std::byte> plain = PlainTerrainAttributes(1, AttributeCells::Bytes);
    plain[TerrainAttributesHeaderSize + WitnessCellOfWorld1] = std::byte{0x00};

    const auto loaded = LoadTerrainAttributes(EncryptTerrainAttributes(plain), 1);

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value()->cells[WitnessCellOfWorld1], 0);
}
} // namespace
