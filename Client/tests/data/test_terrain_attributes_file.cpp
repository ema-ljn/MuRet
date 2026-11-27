#include "Data/Terrain/TerrainAttributesFile.h"

#include "Data/DataError.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "support/SameBytes.h"
#include "support/TerrainFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace
{
using Client::Data::DataError;
using Client::Data::Terrain::CellIndex;
using Client::Data::Terrain::LoadTerrainAttributes;
using Client::Data::Terrain::ReadTerrainAttributesFile;
using Client::Data::Terrain::TerrainAttributesFile;
using Client::Data::Terrain::TerrainAttributesHeaderSize;
using Client::Data::Terrain::TerrainCellCount;
using Client::Data::Terrain::TerrainLayer;
using Client::Data::Terrain::WriteTerrainAttributesFile;
using Client::Tests::AttributeCells;
using Client::Tests::AttributeHighBytePattern;
using Client::Tests::AttributePattern;
using Client::Tests::EncryptTerrainAttributes;
using Client::Tests::PlainTerrainAttributes;
using Client::Tests::SameBytes;

constexpr std::uint8_t Noria = 4;

std::vector<std::byte> NoriaFile(AttributeCells cells)
{
    return EncryptTerrainAttributes(PlainTerrainAttributes(Noria, cells));
}

std::unique_ptr<TerrainAttributesFile> NoriaModel(AttributeCells cells)
{
    auto attributes = std::make_unique<TerrainAttributesFile>();
    attributes->world = Noria;
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        attributes->cells[cell] = AttributePattern(cell);
    }
    if (cells == AttributeCells::Words)
    {
        TerrainLayer<std::uint8_t>& high = attributes->highBytes.emplace();
        for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
        {
            high[cell] = AttributeHighBytePattern(cell);
        }
    }
    return attributes;
}

TEST(TerrainAttributesFileTest, ReadsOneBytePerCell)
{
    const auto read = ReadTerrainAttributesFile(NoriaFile(AttributeCells::Bytes));

    ASSERT_TRUE(read.HasValue());
    const TerrainAttributesFile& attributes = *read.Value();
    EXPECT_EQ(attributes.world, Noria);
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        ASSERT_EQ(attributes.cells[cell], AttributePattern(cell)) << "cell " << cell;
    }
    EXPECT_FALSE(attributes.highBytes.has_value());
}

TEST(TerrainAttributesFileTest, ReadsBothBytesOfEachWord)
{
    const auto read = ReadTerrainAttributesFile(NoriaFile(AttributeCells::Words));

    ASSERT_TRUE(read.HasValue());
    const TerrainAttributesFile& attributes = *read.Value();
    ASSERT_TRUE(attributes.highBytes.has_value());
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        ASSERT_EQ(attributes.cells[cell], AttributePattern(cell)) << "cell " << cell;
        ASSERT_EQ((*attributes.highBytes)[cell], AttributeHighBytePattern(cell)) << "cell " << cell;
    }
}

TEST(TerrainAttributesFileTest, WritesTheLayoutTheLoaderReads)
{
    EXPECT_TRUE(
        SameBytes(WriteTerrainAttributesFile(*NoriaModel(AttributeCells::Bytes)), NoriaFile(AttributeCells::Bytes)));
}

TEST(TerrainAttributesFileTest, WritesWordsWhenTheFileHadThem)
{
    EXPECT_TRUE(
        SameBytes(WriteTerrainAttributesFile(*NoriaModel(AttributeCells::Words)), NoriaFile(AttributeCells::Words)));
}

TEST(TerrainAttributesFileTest, KeepsAnAttributeTheLoaderRefuses)
{
    std::vector<std::byte> plain = PlainTerrainAttributes(Noria, AttributeCells::Bytes);
    plain[TerrainAttributesHeaderSize] = std::byte{0x80};
    const std::vector<std::byte> file = EncryptTerrainAttributes(plain);
    const auto loaded = LoadTerrainAttributes(file, Noria);
    ASSERT_FALSE(loaded.HasValue());
    ASSERT_EQ(loaded.Error(), DataError::InvalidData);

    const auto read = ReadTerrainAttributesFile(file);

    ASSERT_TRUE(read.HasValue());
    EXPECT_EQ(read.Value()->cells[0], 0x80);
    EXPECT_TRUE(SameBytes(WriteTerrainAttributesFile(*read.Value()), file));
}

TEST(TerrainAttributesFileTest, AnyOtherSizeIsAnError)
{
    for (const std::size_t size : {std::size_t{0x10003}, std::size_t{0x10005}, std::size_t{0x20003}})
    {
        const auto read = ReadTerrainAttributesFile(std::vector<std::byte>(size));

        ASSERT_FALSE(read.HasValue()) << size;
        EXPECT_EQ(read.Error(), DataError::UnexpectedSize) << size;
    }
}

TEST(TerrainAttributesFileTest, TheHeaderIs00WorldFFFF)
{
    using Header = std::array<std::byte, TerrainAttributesHeaderSize>;
    for (const Header& header : {Header{std::byte{0x01}, std::byte{Noria}, std::byte{0xFF}, std::byte{0xFF}},
                                 Header{std::byte{0x00}, std::byte{Noria}, std::byte{0xFE}, std::byte{0xFF}},
                                 Header{std::byte{0x00}, std::byte{Noria}, std::byte{0xFF}, std::byte{0xFE}}})
    {
        std::vector<std::byte> plain = PlainTerrainAttributes(Noria, AttributeCells::Bytes);
        std::ranges::copy(header, plain.begin());

        const auto read = ReadTerrainAttributesFile(EncryptTerrainAttributes(plain));

        ASSERT_FALSE(read.HasValue());
        EXPECT_EQ(read.Error(), DataError::InvalidData);
    }
}

TEST(TerrainAttributesFileTest, DoesNotCheckTheWorld)
{
    constexpr std::uint8_t sharedByTwoFiles = 31;

    const auto read = ReadTerrainAttributesFile(
        EncryptTerrainAttributes(PlainTerrainAttributes(sharedByTwoFiles, AttributeCells::Words)));

    ASSERT_TRUE(read.HasValue());
    EXPECT_EQ(read.Value()->world, sharedByTwoFiles);
}

TEST(TerrainAttributesFileTest, TheClientsLoaderReadsAnEdit)
{
    const std::size_t last = CellIndex(255, 255);
    auto read = ReadTerrainAttributesFile(NoriaFile(AttributeCells::Words));
    ASSERT_TRUE(read.HasValue());
    read.Value()->cells[last] = 0x7F;
    const std::vector<std::byte> written = WriteTerrainAttributesFile(*read.Value());

    const auto loaded = LoadTerrainAttributes(written, Noria);
    const auto readAgain = ReadTerrainAttributesFile(written);

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value()->cells[last], 0x7F);
    EXPECT_EQ(loaded.Value()->cells[0], AttributePattern(0));
    ASSERT_TRUE(readAgain.HasValue());
    EXPECT_EQ(readAgain.Value()->highBytes, read.Value()->highBytes);
}
} // namespace
