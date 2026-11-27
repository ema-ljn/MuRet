#include "Data/Terrain/TerrainHeightsFile.h"

#include "Data/DataError.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "support/SameBytes.h"
#include "support/TerrainFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace
{
using Client::Data::DataError;
using Client::Data::Terrain::CellIndex;
using Client::Data::Terrain::LoadTerrainHeights;
using Client::Data::Terrain::ReadTerrainHeightsFile;
using Client::Data::Terrain::TerrainCellCount;
using Client::Data::Terrain::TerrainHeightsBitmapHeaderSize;
using Client::Data::Terrain::TerrainHeightsFile;
using Client::Data::Terrain::TerrainHeightsPrefixSize;
using Client::Data::Terrain::TerrainHeightsScale;
using Client::Data::Terrain::TerrainHeightsSize;
using Client::Data::Terrain::TerrainHeightsValuesOffset;
using Client::Data::Terrain::WriteTerrainHeightsFile;
using Client::Tests::HeightPattern;
using Client::Tests::PlainTerrainHeights;
using Client::Tests::SameBytes;

constexpr std::uint8_t Lorencia = 1;

// PlainTerrainHeights with a prefix and a header of their own, so that a byte read from the wrong part shows.
std::vector<std::byte> HeightsFile()
{
    std::vector<std::byte> file = PlainTerrainHeights();
    for (std::size_t i = 0; i < TerrainHeightsValuesOffset; ++i)
    {
        file[i] = static_cast<std::byte>(i * 7 + 3);
    }
    return file;
}

TEST(TerrainHeightsFileTest, ReadsThePrefixTheHeaderAndTheHeights)
{
    const std::vector<std::byte> file = HeightsFile();
    const std::span<const std::byte> bytes = file;

    const auto read = ReadTerrainHeightsFile(file);

    ASSERT_TRUE(read.HasValue());
    const TerrainHeightsFile& heights = *read.Value();
    EXPECT_TRUE(std::ranges::equal(heights.prefix, bytes.first(TerrainHeightsPrefixSize)));
    EXPECT_TRUE(std::ranges::equal(heights.bitmapHeader,
                                   bytes.subspan(TerrainHeightsPrefixSize, TerrainHeightsBitmapHeaderSize)));
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        ASSERT_EQ(heights.heights[cell], HeightPattern(cell)) << "cell " << cell;
    }
    EXPECT_TRUE(heights.tail.empty());
}

TEST(TerrainHeightsFileTest, WritesWhatItRead)
{
    const std::vector<std::byte> file = HeightsFile();

    const auto read = ReadTerrainHeightsFile(file);

    ASSERT_TRUE(read.HasValue());
    EXPECT_TRUE(SameBytes(WriteTerrainHeightsFile(*read.Value()), file));
}

// 0x5f92b0 reverses the rows twice and 0x5f9020 reads them in order: no flip.
TEST(TerrainHeightsFileTest, HeightsAreRowByRow)
{
    std::vector<std::byte> file = HeightsFile();
    file[TerrainHeightsValuesOffset + 256] = std::byte{0xAB};

    const auto read = ReadTerrainHeightsFile(file);

    ASSERT_TRUE(read.HasValue());
    EXPECT_EQ(read.Value()->heights[CellIndex(0, 1)], 0xAB);
}

TEST(TerrainHeightsFileTest, KeepsTheBytesAfterTheHeights)
{
    const std::vector<std::byte> extra = {std::byte{0x10}, std::byte{0x20}, std::byte{0x30}};
    std::vector<std::byte> file = HeightsFile();
    file.insert(file.end(), extra.begin(), extra.end());

    const auto read = ReadTerrainHeightsFile(file);

    ASSERT_TRUE(read.HasValue());
    EXPECT_EQ(read.Value()->tail, extra);
    EXPECT_TRUE(SameBytes(WriteTerrainHeightsFile(*read.Value()), file));
}

TEST(TerrainHeightsFileTest, AShortFileIsAnError)
{
    std::vector<std::byte> file = HeightsFile();
    file.pop_back();
    ASSERT_EQ(file.size(), TerrainHeightsSize - 1);

    const auto read = ReadTerrainHeightsFile(file);

    ASSERT_FALSE(read.HasValue());
    EXPECT_EQ(read.Error(), DataError::FileTooShort);
}

TEST(TerrainHeightsFileTest, TheClientsLoaderReadsAnEdit)
{
    const std::size_t last = CellIndex(255, 255);
    const auto read = ReadTerrainHeightsFile(HeightsFile());
    ASSERT_TRUE(read.HasValue());
    read.Value()->heights[last] = 200;

    const auto loaded = LoadTerrainHeights(WriteTerrainHeightsFile(*read.Value()), Lorencia);

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(loaded.Value()->cells[last]),
              std::bit_cast<std::uint32_t>(200.0f * TerrainHeightsScale));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(loaded.Value()->cells[0]),
              std::bit_cast<std::uint32_t>(static_cast<float>(HeightPattern(0)) * TerrainHeightsScale));
}
} // namespace
