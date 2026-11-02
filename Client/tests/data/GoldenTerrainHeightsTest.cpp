#include "Data/Terrain/TerrainHeights.h"

#include "Core/ByteReader.h"
#include "Data/Terrain/TerrainGrid.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/TerrainFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// TerrainHeight.OZB reales: los de la carpeta golden y, si está, el volcado de la memoria del
// original (Client/tests/support/GoldenDir.h).
namespace
{
using Client::Core::ByteReader;
using Client::Data::Terrain::LoadTerrainHeights;
using Client::Data::Terrain::TerrainCellCount;
using Client::Data::Terrain::TerrainHeightsBitmapHeaderSize;
using Client::Data::Terrain::TerrainHeightsPrefixSize;
using Client::Data::Terrain::TerrainHeightsScale;
using Client::Data::Terrain::TerrainHeightsSize;
using Client::Data::Terrain::TerrainLayer;
using Client::Tests::BitmapOffBitsOffset;
using Client::Tests::DeviasCapture;
using Client::Tests::FirstCellThatDiffers;
using Client::Tests::LabWorldsWithHeights;
using Client::Tests::LorenciaCapture;
using Client::Tests::MemoryCapture;
using Client::Tests::ReadFileBytes;
using Client::Tests::StandardBitmapHeaderSize;

using GoldenTerrainHeights = Client::Tests::GoldenDataTest;

// La ruta que pide el original: "%s\\TerrainHeight.bmp" con "World%d" (0x6d12a0, en 0x61a560), y
// el cargador cambia la extensión por "OZB".
std::string HeightFile(std::uint8_t world)
{
    return "World" + std::to_string(world) + "\\TerrainHeight.OZB";
}

TEST_F(GoldenTerrainHeights, LoadsEveryWorldOfTheLab)
{
    for (const std::uint8_t world : LabWorldsWithHeights)
    {
        const auto path = DataFile(HeightFile(world));
        ASSERT_TRUE(path.has_value()) << "mundo " << int{world};
        const std::optional<std::vector<char>> file = ReadFileBytes(*path);
        ASSERT_TRUE(file.has_value()) << "mundo " << int{world};
        EXPECT_EQ(file->size(), TerrainHeightsSize) << "mundo " << int{world};

        const auto loaded = LoadTerrainHeights(std::as_bytes(std::span(*file)), world);

        EXPECT_TRUE(loaded.HasValue()) << "mundo " << int{world};
    }
}

// Lo que midió el research en el TerrainHeight.OZB de Lorencia, el mismo archivo que el del
// laboratorio (005f9020-terrainheight-ozb.md §6, con su SHA-256): la altura más alta es 382.5
// (255 por 1,5) y las dos últimas celdas, los dos bytes del final del archivo, son 0.
TEST_F(GoldenTerrainHeights, LorenciaHasWhatTheResearchMeasured)
{
    const auto path = DataFile(HeightFile(1));
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());

    const auto loaded = LoadTerrainHeights(std::as_bytes(std::span(*file)), 1);

    ASSERT_TRUE(loaded.HasValue());
    const TerrainLayer<float>& heights = loaded.Value()->cells;
    EXPECT_EQ(std::ranges::max(heights), 382.5f);
    EXPECT_EQ(heights[TerrainCellCount - 2], 0.0f);
    EXPECT_EQ(heights[TerrainCellCount - 1], 0.0f);
}

// El de World56 llega a 765, el byte 255 por 3: es el único mundo que multiplica por 3.
TEST_F(GoldenTerrainHeights, World56ReachesThreeTimes255)
{
    const auto path = DataFile(HeightFile(56));
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());

    const auto loaded = LoadTerrainHeights(std::as_bytes(std::span(*file)), 56);

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(std::ranges::max(loaded.Value()->cells), 765.0f);
}

// El de World19 son 66.620 ceros, sin BMP: el original lo carga igual, plano (Q-C02).
TEST_F(GoldenTerrainHeights, World19IsFlat)
{
    const auto path = DataFile(HeightFile(19));
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());

    const auto loaded = LoadTerrainHeights(std::as_bytes(std::span(*file)), 19);

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_TRUE(std::ranges::all_of(loaded.Value()->cells, [](float height) { return height == 0.0f; }));
}

class MemoryTerrainHeights : public Client::Tests::MemoryDumpTest
{
protected:
    // Las alturas del TerrainHeight.OZB de la captura son las que tenía en memoria el main.exe
    // original, en 0x07ACCCA4 (`heights`), y `header` es la cabecera que copió a 0x07D0CED8: los
    // 0x438 bytes que siguen al prefijo de 4.
    void ExpectTheOriginalsMemory(const MemoryCapture& capture, std::string_view heights, std::string_view header)
    {
        const auto path = DataFile(HeightFile(capture.world));
        const auto heightDump = DumpFile(capture, heights);
        const auto headerDump = DumpFile(capture, header);
        ASSERT_TRUE(path.has_value());
        ASSERT_TRUE(heightDump.has_value());
        ASSERT_TRUE(headerDump.has_value());
        const std::optional<std::vector<char>> file = ReadFileBytes(*path);
        const std::optional<std::vector<char>> heightMemory = ReadFileBytes(*heightDump);
        const std::optional<std::vector<char>> headerMemory = ReadFileBytes(*headerDump);
        ASSERT_TRUE(file.has_value());
        ASSERT_TRUE(heightMemory.has_value());
        ASSERT_TRUE(headerMemory.has_value());
        ASSERT_EQ(heightMemory->size(), TerrainCellCount * sizeof(float));
        ASSERT_GE(file->size(), TerrainHeightsSize);

        const auto loaded = LoadTerrainHeights(std::as_bytes(std::span(*file)), capture.world);

        ASSERT_TRUE(loaded.HasValue());
        EXPECT_EQ(FirstCellThatDiffers(loaded.Value()->cells, std::as_bytes(std::span(*heightMemory))), std::nullopt);
        const auto bitmapHeader = std::span(*file).subspan(TerrainHeightsPrefixSize, TerrainHeightsBitmapHeaderSize);
        EXPECT_TRUE(std::ranges::equal(bitmapHeader, *headerMemory));
    }
};

TEST_F(MemoryTerrainHeights, EqualsTheOriginalsMemoryInDevias)
{
    ExpectTheOriginalsMemory(DeviasCapture, "s31_height.bin", "s31_bmphdr.bin");
}

TEST_F(MemoryTerrainHeights, EqualsTheOriginalsMemoryInLorencia)
{
    ExpectTheOriginalsMemory(LorenciaCapture, "lor_height.bin", "lor_bmphdr.bin");
}

// Q-C02, con la memoria: leer las alturas desde donde dice bfOffBits da 52.139 de las 65.536
// distintas de las que tenía el original. Una biblioteca de BMP, además, da vuelta las filas: con
// SDL_LoadBMP_IO de SDL3 son 64.500 (TerrainHeights.h).
TEST_F(MemoryTerrainHeights, ReadingFromBfOffBitsWouldNotMatch)
{
    const auto path = DataFile(HeightFile(3));
    const auto heightDump = DumpFile(DeviasCapture, "s31_height.bin");
    ASSERT_TRUE(path.has_value());
    ASSERT_TRUE(heightDump.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    const std::optional<std::vector<char>> heightMemory = ReadFileBytes(*heightDump);
    ASSERT_TRUE(file.has_value());
    ASSERT_TRUE(heightMemory.has_value());
    ASSERT_GE(file->size(), TerrainHeightsSize);
    ASSERT_EQ(heightMemory->size(), TerrainCellCount * sizeof(float));

    const std::span<const std::byte> bytes = std::as_bytes(std::span(*file));
    ByteReader header(bytes.subspan(TerrainHeightsPrefixSize + BitmapOffBitsOffset));
    const std::optional<std::uint32_t> offBits = header.ReadU32Le();
    ASSERT_EQ(offBits, StandardBitmapHeaderSize);
    const std::span<const std::byte> pixels = bytes.subspan(TerrainHeightsPrefixSize + *offBits, TerrainCellCount);
    ByteReader memory(std::as_bytes(std::span(*heightMemory)));
    std::size_t differing = 0;
    for (const std::byte pixel : pixels)
    {
        const float height = static_cast<float>(std::to_integer<std::uint8_t>(pixel)) * TerrainHeightsScale;
        if (memory.ReadU32Le() != std::bit_cast<std::uint32_t>(height))
        {
            ++differing;
        }
    }

    EXPECT_EQ(differing, 52139u);
}
} // namespace
