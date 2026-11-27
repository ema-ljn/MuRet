#include "Data/Terrain/TerrainMappingFile.h"

#include "Data/DataError.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainMapping.h"
#include "support/SameBytes.h"
#include "support/TerrainFiles.h"

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace
{
using Client::Data::DataError;
using Client::Data::Terrain::CellIndex;
using Client::Data::Terrain::LoadTerrainMapping;
using Client::Data::Terrain::ReadTerrainMappingFile;
using Client::Data::Terrain::TerrainAlphaScale;
using Client::Data::Terrain::TerrainCellCount;
using Client::Data::Terrain::TerrainMappingFile;
using Client::Data::Terrain::WriteTerrainMappingFile;
using Client::Tests::AlphaPattern;
using Client::Tests::EncryptK16;
using Client::Tests::Layer1Pattern;
using Client::Tests::Layer2Pattern;
using Client::Tests::PlainTerrainMapping;
using Client::Tests::SameBytes;

constexpr std::uint8_t Devias = 3;

std::vector<std::byte> DeviasFile()
{
    return EncryptK16(PlainTerrainMapping(Devias));
}

TEST(TerrainMappingFileTest, ReadsTheLayersOfTheFile)
{
    const auto read = ReadTerrainMappingFile(DeviasFile());

    ASSERT_TRUE(read.HasValue());
    const TerrainMappingFile& mapping = *read.Value();
    EXPECT_EQ(mapping.version, 0);
    EXPECT_EQ(mapping.world, Devias);
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        ASSERT_EQ(mapping.layer1[cell], Layer1Pattern(cell)) << "layer 1, cell " << cell;
        ASSERT_EQ(mapping.layer2[cell], Layer2Pattern(cell)) << "layer 2, cell " << cell;
        ASSERT_EQ(mapping.alpha[cell], AlphaPattern(cell)) << "alpha, cell " << cell;
    }
    EXPECT_TRUE(mapping.tail.empty());
}

TEST(TerrainMappingFileTest, WritesTheLayoutOf0x5f8960)
{
    constexpr std::uint8_t anotherWorld = 7;
    auto mapping = std::make_unique<TerrainMappingFile>();
    mapping->world = anotherWorld;
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        mapping->layer1[cell] = Layer1Pattern(cell);
        mapping->layer2[cell] = Layer2Pattern(cell);
        mapping->alpha[cell] = AlphaPattern(cell);
    }

    EXPECT_TRUE(SameBytes(WriteTerrainMappingFile(*mapping), EncryptK16(PlainTerrainMapping(anotherWorld))));
}

TEST(TerrainMappingFileTest, KeepsTheBytesAfterTheLayout)
{
    const std::vector<std::byte> extra = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
                                          std::byte{5}, std::byte{6}, std::byte{7}};
    std::vector<std::byte> plain = PlainTerrainMapping(Devias);
    plain.insert(plain.end(), extra.begin(), extra.end());
    const std::vector<std::byte> file = EncryptK16(plain);

    const auto read = ReadTerrainMappingFile(file);

    ASSERT_TRUE(read.HasValue());
    EXPECT_EQ(read.Value()->tail, extra);
    EXPECT_TRUE(SameBytes(WriteTerrainMappingFile(*read.Value()), file));
}

TEST(TerrainMappingFileTest, KeepsTheFirstByte)
{
    std::vector<std::byte> plain = PlainTerrainMapping(Devias);
    plain[0] = std::byte{0x2A};
    const std::vector<std::byte> file = EncryptK16(plain);

    const auto read = ReadTerrainMappingFile(file);

    ASSERT_TRUE(read.HasValue());
    EXPECT_EQ(read.Value()->version, 0x2A);
    EXPECT_TRUE(SameBytes(WriteTerrainMappingFile(*read.Value()), file));
}

TEST(TerrainMappingFileTest, DoesNotCheckTheWorld)
{
    constexpr std::uint8_t notAWorld = 200;

    const auto read = ReadTerrainMappingFile(EncryptK16(PlainTerrainMapping(notAWorld)));

    ASSERT_TRUE(read.HasValue());
    EXPECT_EQ(read.Value()->world, notAWorld);
}

TEST(TerrainMappingFileTest, AShortFileIsAnError)
{
    std::vector<std::byte> file = DeviasFile();
    file.pop_back();

    const auto read = ReadTerrainMappingFile(file);

    ASSERT_FALSE(read.HasValue());
    EXPECT_EQ(read.Error(), DataError::FileTooShort);
}

TEST(TerrainMappingFileTest, TheClientsLoaderReadsAnEdit)
{
    const std::size_t last = CellIndex(255, 255);
    const std::size_t first = CellIndex(0, 0);
    auto read = ReadTerrainMappingFile(DeviasFile());
    ASSERT_TRUE(read.HasValue());
    TerrainMappingFile& mapping = *read.Value();
    mapping.layer2[last] = 0x11;
    mapping.alpha[first] = 255;

    const auto loaded = LoadTerrainMapping(WriteTerrainMappingFile(mapping), Devias);

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value()->layer2[last], 0x11);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(loaded.Value()->alpha[first]),
              std::bit_cast<std::uint32_t>(255.0f * TerrainAlphaScale));
    EXPECT_EQ(loaded.Value()->layer1[1], Layer1Pattern(1));
    EXPECT_EQ(loaded.Value()->layer2[1], Layer2Pattern(1));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(loaded.Value()->alpha[1]),
              std::bit_cast<std::uint32_t>(static_cast<float>(AlphaPattern(1)) * TerrainAlphaScale));
}
} // namespace
