// Client::Data::Terrain::LoadTerrainMapping: EncTerrain.map, con archivos sintéticos.

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

// Las tres capas, cada una con su patrón (TerrainFiles.h), en el orden del archivo.
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

// La celda (x, y) está en el byte y * 256 + x de cada capa, y CellIndex la encuentra ahí.
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

// Los bits de cada float, sacados a mano: el byte por 0x3B808081, redondeado a float.
TEST(TerrainMappingTest, AlphaIsTheByteTimesTheOriginalsConstant)
{
    struct Case
    {
        std::uint8_t byte;
        std::uint32_t bits;
    };
    constexpr std::array<Case, 6> cases{{
        {0, 0x00000000},  // 0.0f
        {1, 0x3B808081},  // la constante misma
        {51, 0x3E4CCCCE}, // 0.2f sería 0x3E4CCCCD: ver abajo
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

// Dividir por 255 parece lo mismo y no lo es: con 51 da el float más cercano a 0.2, y el original
// da el de al lado. Por eso TerrainAlphaScale multiplica.
static_assert(std::bit_cast<std::uint32_t>(51.0f / 255.0f) == 0x3E4CCCCD);

// El byte 0 es la versión del formato y el original no lo lee: con otro valor, el mapeo es igual.
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

// Q-C24: un archivo corto es un error. El original leería después de su buffer.
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

// Los bytes de más, cifrados en el mismo flujo, se ignoran.
TEST(TerrainMappingTest, ExtraBytesAreIgnored)
{
    std::vector<std::byte> plain = PlainTerrainMapping(Devias);
    plain.resize(TerrainMappingSize + 100, std::byte{0x55});

    const auto loaded = LoadTerrainMapping(EncryptK16(plain), Devias);

    ASSERT_TRUE(loaded.HasValue());
    ExpectThePatterns(*loaded.Value());
}

} // namespace
