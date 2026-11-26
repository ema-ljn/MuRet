#include "Data/Terrain/TerrainMapping.h"

#include "Data/DataError.h"
#include "Data/Terrain/TerrainGrid.h"
#include "support/TerrainFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace
{
using Client::Data::DataError;
using Client::Data::Terrain::CellIndex;
using Client::Data::Terrain::LoadTerrainMapping;
using Client::Data::Terrain::TerrainAlphaScale;
using Client::Data::Terrain::TerrainCellCount;
using Client::Data::Terrain::TerrainMapping;
using Client::Data::Terrain::TerrainMappingAlphaOffset;
using Client::Data::Terrain::TerrainMappingLayer1Offset;
using Client::Data::Terrain::TerrainMappingSize;
using Client::Data::Terrain::TerrainMappingWorldOffset;
using Client::Tests::AlphaPattern;
using Client::Tests::EncryptK16;
using Client::Tests::Layer1Pattern;
using Client::Tests::Layer2Pattern;
using Client::Tests::PlainTerrainMapping;

constexpr std::uint8_t Devias = 3;

// Each layer with its pattern (TerrainFiles.h), in file order.
void ExpectThePatterns(const TerrainMapping& mapping)
{
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        ASSERT_EQ(mapping.layer1[cell], Layer1Pattern(cell)) << "capa 1, celda " << cell;
        ASSERT_EQ(mapping.layer2[cell], Layer2Pattern(cell)) << "capa 2, celda " << cell;
        ASSERT_EQ(mapping.alpha[cell], static_cast<float>(AlphaPattern(cell)) * TerrainAlphaScale)
            << "alfa, celda " << cell;
    }
}

TEST(TerrainMappingTest, ReadsTheThreeLayersInFileOrder)
{
    const auto loaded = LoadTerrainMapping(EncryptK16(PlainTerrainMapping(Devias)), Devias);

    ASSERT_TRUE(loaded.HasValue());
    ExpectThePatterns(*loaded.Value());
}

TEST(TerrainMappingTest, CellXYIsRowByRow)
{
    constexpr std::uint8_t x = 3;
    constexpr std::uint8_t y = 200;
    std::vector<std::byte> plain(TerrainMappingSize);
    plain[TerrainMappingWorldOffset] = std::byte{Devias};
    plain[TerrainMappingLayer1Offset + std::size_t{y} * 256 + std::size_t{x}] = std::byte{0x2A};

    const auto loaded = LoadTerrainMapping(EncryptK16(plain), Devias);

    ASSERT_TRUE(loaded.HasValue());
    const TerrainMapping& mapping = *loaded.Value();
    EXPECT_EQ(mapping.layer1[CellIndex(x, y)], 0x2A);
    EXPECT_EQ(mapping.layer1[CellIndex(y, x)], 0) << "x e y no se pueden cambiar";
}

// Each float's bits, worked out by hand: the byte times 0x3B808081, rounded to float.
TEST(TerrainMappingTest, AlphaIsTheByteTimesTheOriginalsConstant)
{
    struct Case
    {
        std::uint8_t byte;
        std::uint32_t bits;
    };
    constexpr std::array<Case, 6> cases{{
        {0, 0x00000000},  // 0.0f
        {1, 0x3B808081},  // the constant itself
        {51, 0x3E4CCCCE}, // 0.2f would be 0x3E4CCCCD: see below
        {128, 0x3F008081},
        {254, 0x3F7EFF00},
        {255, 0x3F800000}, // 1.0f justo
    }};
    std::vector<std::byte> plain(TerrainMappingSize);
    plain[TerrainMappingWorldOffset] = std::byte{Devias};
    for (std::size_t cell = 0; cell < cases.size(); ++cell)
    {
        plain[TerrainMappingAlphaOffset + cell] = std::byte{cases[cell].byte};
    }

    const auto loaded = LoadTerrainMapping(EncryptK16(plain), Devias);

    ASSERT_TRUE(loaded.HasValue());
    for (std::size_t cell = 0; cell < cases.size(); ++cell)
    {
        EXPECT_EQ(std::bit_cast<std::uint32_t>(loaded.Value()->alpha[cell]), cases[cell].bits)
            << "byte " << int{cases[cell].byte};
    }
}

// Dividing by 255 looks the same and is not: with 51 it gives the float nearest 0.2, and the original gives the next
// one. That is why TerrainAlphaScale multiplies.
static_assert(std::bit_cast<std::uint32_t>(51.0f / 255.0f) == 0x3E4CCCCD);

// Byte 0 is the format version and the original does not read it.
TEST(TerrainMappingTest, TheVersionByteIsNotRead)
{
    std::vector<std::byte> plain = PlainTerrainMapping(Devias);
    plain[0] = std::byte{0x7F};

    const auto loaded = LoadTerrainMapping(EncryptK16(plain), Devias);

    ASSERT_TRUE(loaded.HasValue());
    ExpectThePatterns(*loaded.Value());
}

TEST(TerrainMappingTest, AnotherWorldIsAnError)
{
    const auto loaded = LoadTerrainMapping(EncryptK16(PlainTerrainMapping(2)), Devias);

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_EQ(loaded.Error(), DataError::WrongWorld);
}

// Q-C24: the original would read past its buffer.
TEST(TerrainMappingTest, ShortFileIsAnError)
{
    std::vector<std::byte> oneByteShort = EncryptK16(PlainTerrainMapping(Devias));
    oneByteShort.pop_back();

    const auto shortLoaded = LoadTerrainMapping(oneByteShort, Devias);
    const auto empty = LoadTerrainMapping(std::span<const std::byte>{}, Devias);

    ASSERT_FALSE(shortLoaded.HasValue());
    EXPECT_EQ(shortLoaded.Error(), DataError::FileTooShort);
    ASSERT_FALSE(empty.HasValue());
    EXPECT_EQ(empty.Error(), DataError::FileTooShort);
}

// Encrypted in the same stream.
TEST(TerrainMappingTest, ExtraBytesAreIgnored)
{
    std::vector<std::byte> plain = PlainTerrainMapping(Devias);
    plain.resize(TerrainMappingSize + 100, std::byte{0x55});

    const auto loaded = LoadTerrainMapping(EncryptK16(plain), Devias);

    ASSERT_TRUE(loaded.HasValue());
    ExpectThePatterns(*loaded.Value());
}
} // namespace
