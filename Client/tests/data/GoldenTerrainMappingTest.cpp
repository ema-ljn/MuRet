// EncTerrain.map reales: los de la carpeta golden y, si está, el volcado de la memoria del original
// (Client/tests/support/GoldenDir.h).

#include "Data/Terrain/TerrainMapping.h"

#include "Data/Terrain/TerrainGrid.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/TerrainFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace
{

using Client::Data::Terrain::LoadTerrainMapping;
using Client::Data::Terrain::TerrainCellCount;
using Client::Data::Terrain::TerrainMapping;
using Client::Data::Terrain::TerrainMappingSize;
using Client::Tests::FirstCellThatDiffers;
using Client::Tests::LabWorlds;
using Client::Tests::ReadFileBytes;

using GoldenTerrainMapping = Client::Tests::GoldenDataTest;
using MemoryTerrainMapping = Client::Tests::MemoryDumpTest;

// La ruta que arma el original: "Data\\%s\\EncTerrain%d.map" con "World%d" (0x6d12e8, en 0x61a560).
std::string MappingFile(std::uint8_t world)
{
    const std::string number = std::to_string(world);
    return "World" + number + "\\EncTerrain" + number + ".map";
}

TEST_F(GoldenTerrainMapping, LoadsEveryWorldOfTheLab)
{
    for (const std::uint8_t world : LabWorlds)
    {
        const auto path = DataFile(MappingFile(world));
        ASSERT_TRUE(path.has_value()) << "mundo " << int{world};
        const std::optional<std::vector<char>> file = ReadFileBytes(*path);
        ASSERT_TRUE(file.has_value()) << "mundo " << int{world};
        EXPECT_EQ(file->size(), TerrainMappingSize) << "mundo " << int{world};

        const auto loaded = LoadTerrainMapping(std::as_bytes(std::span(*file)), world);

        EXPECT_TRUE(loaded.HasValue()) << "mundo " << int{world};
    }
}

// Los conteos de Lorencia que midió el research con su propio descifrado
// (005f87b0-encterrain-map.md §8): las celdas de agua (tile 5), las que no tienen capa 2, las que
// tienen alfa, y las que tienen alfa sin capa 2.
TEST_F(GoldenTerrainMapping, LorenciaHasTheCountsOfTheResearch)
{
    const auto path = DataFile(MappingFile(1));
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());

    const auto loaded = LoadTerrainMapping(std::as_bytes(std::span(*file)), 1);

    ASSERT_TRUE(loaded.HasValue());
    const TerrainMapping& mapping = *loaded.Value();
    std::size_t alphaWithoutLayer2 = 0;
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        if (mapping.alpha[cell] > 0.0f && mapping.layer2[cell] == 0xFF)
        {
            ++alphaWithoutLayer2;
        }
    }
    EXPECT_EQ(std::ranges::count(mapping.layer1, 5), 7939);
    EXPECT_EQ(std::ranges::count(mapping.layer2, 0xFF), 43785);
    EXPECT_EQ(std::ranges::count_if(mapping.alpha, [](float alpha) { return alpha > 0.0f; }), 14629);
    EXPECT_EQ(alphaWithoutLayer2, 73u);
}

// Las capas son las que tenía en memoria el main.exe original después de cargar el mismo archivo,
// en Devias (World3). El original guarda la capa 2 en 0x07AACC8C y la capa 1 justo después, en
// 0x07ABCC8C: s31_l2l1.bin es las dos en ese orden. s31_alpha.bin son los floats de 0x07CCCCC4.
TEST_F(MemoryTerrainMapping, EqualsTheOriginalsMemory)
{
    const auto path = DataFile(MappingFile(3));
    const auto layers = DumpFile("s31_l2l1.bin");
    const auto alpha = DumpFile("s31_alpha.bin");
    ASSERT_TRUE(path.has_value());
    ASSERT_TRUE(layers.has_value());
    ASSERT_TRUE(alpha.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    const std::optional<std::vector<char>> layersMemory = ReadFileBytes(*layers);
    const std::optional<std::vector<char>> alphaMemory = ReadFileBytes(*alpha);
    ASSERT_TRUE(file.has_value());
    ASSERT_TRUE(layersMemory.has_value());
    ASSERT_TRUE(alphaMemory.has_value());
    ASSERT_EQ(layersMemory->size(), 2 * TerrainCellCount);
    ASSERT_EQ(alphaMemory->size(), TerrainCellCount * sizeof(float));

    const auto loaded = LoadTerrainMapping(std::as_bytes(std::span(*file)), 3);

    ASSERT_TRUE(loaded.HasValue());
    const TerrainMapping& mapping = *loaded.Value();
    const std::span<const std::byte> both = std::as_bytes(std::span(*layersMemory));
    EXPECT_EQ(FirstCellThatDiffers(mapping.layer2, both.first(TerrainCellCount)), std::nullopt);
    EXPECT_EQ(FirstCellThatDiffers(mapping.layer1, both.subspan(TerrainCellCount)), std::nullopt);
    EXPECT_EQ(FirstCellThatDiffers(mapping.alpha, std::as_bytes(std::span(*alphaMemory))), std::nullopt);
}

} // namespace
