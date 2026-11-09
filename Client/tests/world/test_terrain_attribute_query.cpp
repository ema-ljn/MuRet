#include "World/TerrainAttributeQuery.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "World/ITerrainAttributeQuerySource.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <limits>
#include <cstdint>
#include <memory>
#include <vector>

namespace
{
class Source final : public Client::World::ITerrainAttributeQuerySource
{
public:
    std::unique_ptr<Client::Data::Terrain::TerrainAttributes> table =
        std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    std::vector<int> events;
    std::array<float, 2> scales{-0.01f, -0.01f};
    std::size_t scaleIndex = 0;
    float ReadScale() override
    {
        events.push_back(1);
        return scales.at(scaleIndex++);
    }
    const Client::Data::Terrain::TerrainAttributes& Attributes() override
    {
        events.push_back(2);
        return *table;
    }
};
} // namespace

TEST(TerrainAttributeQueryTest, ReturnsFullWordInAddressHighHalf)
{
    Source source;
    // Y=350 -> -3, X=250 -> -2: word 770, dirección nativa 0x07d0d94c.
    source.table->cells[770] = 0xab08;
    const auto result = Client::World::TerrainAttributeQueryAt(source, 250.0f, 350.0f);
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value(), 0x07d0ab08U);
    EXPECT_EQ(source.events, (std::vector<int>{1, 1, 2}));
}

TEST(TerrainAttributeQueryTest, PreservesWordClassesAndReadsLiveTableEveryCall)
{
    Source source;
    const std::array<std::uint16_t, 5> words{0, 1, 8, 0x8000, 0xffff};
    for (const auto word : words)
    {
        source.scaleIndex = 0;
        source.table->cells[0] = word;
        const auto result = Client::World::TerrainAttributeQueryAt(source, 0.0f, 0.0f);
        ASSERT_TRUE(result.HasValue());
        EXPECT_EQ(result.Value(), 0x07d00000U | word);
    }
}

TEST(TerrainAttributeQueryTest, TruncatesExactBinary32ProductWithoutFloatStore)
{
    Source source;
    source.table->cells[0] = 0x1111;
    source.table->cells[1] = 0x2222;
    // 100 * K es mayor que -1; un store float intermedio lo redondearía a -1.
    const auto result = Client::World::TerrainAttributeQueryAt(source, 100.0f, 0.0f);
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value(), 0x07d01111U);
}

TEST(TerrainAttributeQueryTest, RereadsScaleForXAfterY)
{
    Source source;
    source.scales = {-1.0f, -2.0f};
    source.table->cells[778] = 0x3456;
    const auto result = Client::World::TerrainAttributeQueryAt(source, 5.0f, 3.0f);
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value(), 0x07d03456U);
    EXPECT_EQ(source.events, (std::vector<int>{1, 1, 2}));
}

TEST(TerrainAttributeQueryTest, CombinesSignedCoordinatesBeforeCheckingAddress)
{
    Source source;
    source.scales = {1.0f, 1.0f};
    source.table->cells[0] = 0x78ab;
    // X=256 compensa una fila negativa; no hay módulo por eje ni guarda signed.
    const auto result = Client::World::TerrainAttributeQueryAt(source, 256.0f, -1.0f);
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value(), 0x07d078abU);
}

TEST(TerrainAttributeQueryTest, UsesLow32OfSigned64AndWrapsByteOffset)
{
    Source source;
    source.scales = {1.0f, 1.0f};
    source.table->cells[0] = 0x4567;
    // +/-2^32 pierden el dword alto; -2^31 duplica a cero con wrap uint32.
    const std::array<float, 4> coordinates{0x1p32f, -0x1p32f, -0x1p31f, -0x1p63f};
    for (const float x : coordinates)
    {
        source.scaleIndex = 0;
        const auto result = Client::World::TerrainAttributeQueryAt(source, x, 0.0f);
        ASSERT_TRUE(result.HasValue());
        EXPECT_EQ(result.Value(), 0x07d04567U);
    }
}

TEST(TerrainAttributeQueryTest, ContinuesAfterMaskedInvalidConversion)
{
    Source source;
    source.scales = {1.0f, 1.0f};
    source.table->cells[256] = 0x80ff;
    const std::array<float, 4> coordinates{std::numeric_limits<float>::quiet_NaN(),
                                           std::numeric_limits<float>::infinity(),
                                           -std::numeric_limits<float>::infinity(), 0x1p63f};
    for (const float x : coordinates)
    {
        source.scaleIndex = 0;
        const auto result = Client::World::TerrainAttributeQueryAt(source, x, -1.0f);
        ASSERT_TRUE(result.HasValue());
        EXPECT_EQ(result.Value(), 0x07d080ffU);
        EXPECT_EQ(source.scaleIndex, 2U);
    }
}

TEST(TerrainAttributeQueryTest, AcceptsLastWordAndPreservesCalculatedAddressHighHalf)
{
    Source source;
    source.scales = {1.0f, 1.0f};
    source.table->cells[65535] = 0xcdef;
    const auto result = Client::World::TerrainAttributeQueryAt(source, -65535.0f, 0.0f);
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value(), 0x07d2cdefU);
}

TEST(TerrainAttributeQueryTest, ReportsOnlyUnrepresentedFinalAddressesAfterBothConversions)
{
    const std::array<float, 2> coordinates{1.0f, -65536.0f};
    for (const float x : coordinates)
    {
        Source source;
        source.scales = {1.0f, 1.0f};
        const auto result = Client::World::TerrainAttributeQueryAt(source, x, 0.0f);
        ASSERT_FALSE(result.HasValue());
        EXPECT_EQ(result.Error(), Client::World::TerrainAttributeQueryError::UnrepresentedAddress);
        EXPECT_EQ(source.events, (std::vector<int>{1, 1, 2}));
    }
}

TEST(TerrainAttributeQueryTest, TruncatesYProductWithoutFloatStore)
{
    Source source;
    source.table->cells[0] = 0x1357;
    source.table->cells[256] = 0x2468;
    // K=0xbc23d70a: 100*K=-0.999999977648258209228515625, trunca a cero.
    // Un store binary32 antes de FTOL daría -1 y seleccionaría la celda 256.
    const auto result = Client::World::TerrainAttributeQueryAt(source, 0.0f, 100.0f);
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value(), 0x07d01357U);
    EXPECT_EQ(source.events, (std::vector<int>{1, 1, 2}));
}

TEST(TerrainAttributeQueryTest, KeepsNonzeroLow32FromLargeX)
{
    Source source;
    source.scales = {1.0f, 1.0f};
    source.table->cells[0] = 0x1111;
    source.table->cells[512] = 0x5a08;
    // -(2^32+512) es binary32 exacto: low32=0xfffffe00, offset=1024 bytes.
    const auto result = Client::World::TerrainAttributeQueryAt(source, -4294967808.0f, 0.0f);
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value(), 0x07d05a08U);
    EXPECT_EQ(source.events, (std::vector<int>{1, 1, 2}));
}

TEST(TerrainAttributeQueryTest, WrapsLargeRowShiftToNonzeroOffset)
{
    Source source;
    source.scales = {1.0f, 1.0f};
    source.table->cells[0] = 0x2222;
    source.table->cells[512] = 0x6b19;
    // Y=-(2^24+2): low32=0xfefffffe; shift8 con wrap=0xfffffe00.
    const auto result = Client::World::TerrainAttributeQueryAt(source, 0.0f, -16777218.0f);
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value(), 0x07d06b19U);
    EXPECT_EQ(source.events, (std::vector<int>{1, 1, 2}));
}

TEST(TerrainAttributeQueryTest, KeepsNonzeroLow32FromLargeYBeforeCombiningX)
{
    Source source;
    source.scales = {1.0f, 1.0f};
    source.table->cells[0] = 0x3333;
    source.table->cells[512] = 0x7c2a;
    // Y=-(2^32+512): low32=0xfffffe00, shift8=0xfffe0000 (-131072).
    // X=130560 compensa hasta índice -512; no descartar Y por estar fuera de signed32.
    const auto result = Client::World::TerrainAttributeQueryAt(source, 130560.0f, -4294967808.0f);
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value(), 0x07d07c2aU);
    EXPECT_EQ(source.events, (std::vector<int>{1, 1, 2}));
}

TEST(TerrainAttributeQueryTest, ContinuesAfterMaskedInvalidYConversion)
{
    const std::array<float, 5> coordinates{std::numeric_limits<float>::quiet_NaN(),
                                           std::numeric_limits<float>::infinity(),
                                           -std::numeric_limits<float>::infinity(), 0x1p63f, -0x1p63f};
    for (const float y : coordinates)
    {
        Source source;
        source.scales = {1.0f, 1.0f};
        source.table->cells[0] = 0x4444;
        source.table->cells[1] = 0x8d3b;
        // Invalid enmascarada da lowY=0; -2^63 válido también tiene low32 cero.
        // X=-1 selecciona celda 1: la segunda lectura/conversión debe completarse.
        const auto result = Client::World::TerrainAttributeQueryAt(source, -1.0f, y);
        ASSERT_TRUE(result.HasValue());
        EXPECT_EQ(result.Value(), 0x07d08d3bU);
        EXPECT_EQ(source.events, (std::vector<int>{1, 1, 2}));
    }
}
