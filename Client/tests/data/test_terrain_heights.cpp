#include "Data/Terrain/TerrainHeights.h"

#include "Data/DataError.h"
#include "Data/Terrain/TerrainGrid.h"
#include "support/TerrainFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{
using Client::Data::DataError;
using Client::Data::Terrain::CellIndex;
using Client::Data::Terrain::LoadTerrainHeights;
using Client::Data::Terrain::TerrainCellCount;
using Client::Data::Terrain::TerrainHeights;
using Client::Data::Terrain::TerrainHeightsPrefixSize;
using Client::Data::Terrain::TerrainHeightsSize;
using Client::Data::Terrain::TerrainHeightsValuesOffset;
using Client::Tests::BitmapOffBitsOffset;
using Client::Tests::HeightPattern;
using Client::Tests::PlainTerrainHeights;
using Client::Tests::StandardBitmapHeaderSize;

constexpr std::uint8_t Devias = 3;
constexpr std::uint8_t World56 = 56;

void ExpectThePattern(const TerrainHeights& heights, float scale)
{
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        ASSERT_EQ(heights.cells[cell], static_cast<float>(HeightPattern(cell)) * scale) << "celda " << cell;
    }
}

TEST(TerrainHeightsTest, EachByteTimes1Point5)
{
    const auto loaded = LoadTerrainHeights(PlainTerrainHeights(), Devias);

    ASSERT_TRUE(loaded.HasValue());
    ExpectThePattern(*loaded.Value(), 1.5f);
}

// Only World56 (the original's 0x37) multiplies by 3; all others by 1.5.
TEST(TerrainHeightsTest, OnlyWorld56TimesThree)
{
    const std::vector<std::byte> file = PlainTerrainHeights();
    constexpr std::size_t Cell = 250;
    ASSERT_EQ(HeightPattern(Cell), 250);
    for (unsigned world = 0; world < 256; ++world)
    {
        const auto loaded = LoadTerrainHeights(file, static_cast<std::uint8_t>(world));

        ASSERT_TRUE(loaded.HasValue()) << "mundo " << world;
        EXPECT_EQ(loaded.Value()->cells[Cell], world == World56 ? 750.0f : 375.0f) << "mundo " << world;
    }
    const auto world56 = LoadTerrainHeights(file, World56);
    ASSERT_TRUE(world56.HasValue());
    ExpectThePattern(*world56.Value(), 3.0f);
}

// 255 gives 382.5, the maximum the research measured in Lorencia, and times 3, 765.
TEST(TerrainHeightsTest, EveryByteGivesAnExactHeight)
{
    std::vector<std::byte> file = PlainTerrainHeights();
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        file[TerrainHeightsValuesOffset + cell] = static_cast<std::byte>(cell);
    }

    const auto normal = LoadTerrainHeights(file, Devias);
    const auto doubled = LoadTerrainHeights(file, World56);

    ASSERT_TRUE(normal.HasValue());
    ASSERT_TRUE(doubled.HasValue());
    for (unsigned byte = 0; byte < 256; ++byte)
    {
        EXPECT_EQ(normal.Value()->cells[byte], static_cast<float>(3 * byte) / 2.0f) << byte;
        EXPECT_EQ(doubled.Value()->cells[byte], static_cast<float>(3 * byte)) << byte;
    }
    EXPECT_EQ(normal.Value()->cells[255], 382.5f);
}

// Cell (x, y) is y * 256 + x from 0x43C. The original does not flip the rows, though a BMP stores the bottom one
// first.
TEST(TerrainHeightsTest, CellXYIsRowByRow)
{
    constexpr std::uint8_t x = 3;
    constexpr std::uint8_t y = 200;
    std::vector<std::byte> file = PlainTerrainHeights();
    file[TerrainHeightsValuesOffset + std::size_t{y} * 256 + std::size_t{x}] = std::byte{0xFE};

    const auto loaded = LoadTerrainHeights(file, Devias);

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value()->cells[CellIndex(x, y)], 381.0f);
}

// Q-C02: a standard BMP says in bfOffBits that its pixels start at 0x436, but the original reads from 0x438. With
// the pixels where the BMP says, each height is that of the pixel two places later, and the last two cells are the
// file's last two bytes.
TEST(TerrainHeightsTest, IgnoresBfOffBits)
{
    constexpr std::size_t OffBits = TerrainHeightsPrefixSize + BitmapOffBitsOffset;
    std::vector<std::byte> file(TerrainHeightsSize, std::byte{0x00});
    file[OffBits] = static_cast<std::byte>(StandardBitmapHeaderSize & 0xFF);
    file[OffBits + 1] = static_cast<std::byte>(StandardBitmapHeaderSize >> 8 & 0xFF);
    const std::size_t pixels = TerrainHeightsPrefixSize + StandardBitmapHeaderSize;
    for (std::size_t pixel = 0; pixel < TerrainCellCount; ++pixel)
    {
        file[pixels + pixel] = std::byte{HeightPattern(pixel)};
    }
    file[TerrainHeightsSize - 2] = std::byte{0xFC};
    file[TerrainHeightsSize - 1] = std::byte{0xFD};

    const auto loaded = LoadTerrainHeights(file, Devias);

    ASSERT_TRUE(loaded.HasValue());
    const TerrainHeights& heights = *loaded.Value();
    EXPECT_EQ(heights.cells[0], static_cast<float>(HeightPattern(2)) * 1.5f);
    EXPECT_EQ(heights.cells[CellIndex(255, 0)], static_cast<float>(HeightPattern(CellIndex(1, 1))) * 1.5f);
    EXPECT_EQ(heights.cells[TerrainCellCount - 2], 252.0f * 1.5f);
    EXPECT_EQ(heights.cells[TerrainCellCount - 1], 253.0f * 1.5f);
}

// Q-C02: the header is ignored. An all-zero file, like World19's, loads with every height zero.
TEST(TerrainHeightsTest, DoesNotLookAtTheHeader)
{
    const auto loaded = LoadTerrainHeights(std::vector<std::byte>(TerrainHeightsSize), Devias);

    ASSERT_TRUE(loaded.HasValue());
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        ASSERT_EQ(loaded.Value()->cells[cell], 0.0f) << "celda " << cell;
    }
}

// Q-C24: a missing byte, even the last cell's, is an error.
TEST(TerrainHeightsTest, AShortFileIsAnError)
{
    for (const std::size_t size : {std::size_t{0}, TerrainHeightsValuesOffset, TerrainHeightsSize - 1})
    {
        const auto loaded = LoadTerrainHeights(std::vector<std::byte>(size), Devias);

        ASSERT_FALSE(loaded.HasValue()) << size << " bytes";
        EXPECT_EQ(loaded.Error(), DataError::FileTooShort) << size << " bytes";
    }
}

TEST(TerrainHeightsTest, IgnoresTheBytesAfterTheLastCell)
{
    std::vector<std::byte> file = PlainTerrainHeights();
    file.push_back(std::byte{0xFF});

    const auto loaded = LoadTerrainHeights(file, Devias);

    ASSERT_TRUE(loaded.HasValue());
    ExpectThePattern(*loaded.Value(), 1.5f);
}
} // namespace
