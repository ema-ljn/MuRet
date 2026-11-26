#include "Data/Terrain/TerrainAttributes.h"

#include "Core/ByteReader.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainMapping.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/TerrainFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using Client::Core::ByteReader;
using Client::Data::Terrain::LoadTerrainAttributes;
using Client::Data::Terrain::LoadTerrainMapping;
using Client::Data::Terrain::TerrainAttributes;
using Client::Data::Terrain::TerrainAttributesSizeWithByteCells;
using Client::Data::Terrain::TerrainAttributesSizeWithWordCells;
using Client::Data::Terrain::TerrainCellCount;
using Client::Data::Terrain::TerrainSide;
using Client::Tests::DeviasCapture;
using Client::Tests::LabWorlds;
using Client::Tests::LorenciaCapture;
using Client::Tests::MemoryCapture;
using Client::Tests::ReadFileBytes;

using GoldenTerrainAttributes = Client::Tests::GoldenDataTest;

// The path the original builds: "Data\\%s\\EncTerrain%d.att" with "World%d" (0x6c7314, at 0x61a560).
std::string AttributesFile(std::uint8_t world)
{
    const std::string number = std::to_string(world);
    return "World" + number + "\\EncTerrain" + number + ".att";
}

// In LabWorlds, world 1 and those from 31 on have two bytes per cell; the rest, one. 31 and 56 have cells whose high
// byte is not zero, and it is dropped.

TEST_F(GoldenTerrainAttributes, LoadsEveryWorldOfTheLab)
{
    for (const std::uint8_t world : LabWorlds)
    {
        const auto path = DataFile(AttributesFile(world));
        ASSERT_TRUE(path.has_value()) << "mundo " << int{world};
        const std::optional<std::vector<char>> file = ReadFileBytes(*path);
        ASSERT_TRUE(file.has_value()) << "mundo " << int{world};
        const bool words = world == 1 || world >= 31;
        EXPECT_EQ(file->size(), words ? TerrainAttributesSizeWithWordCells : TerrainAttributesSizeWithByteCells)
            << "mundo " << int{world};

        const auto loaded = LoadTerrainAttributes(std::as_bytes(std::span(*file)), world);

        EXPECT_TRUE(loaded.HasValue()) << "mundo " << int{world};
    }
}

// The original picks these by game state (0x61a6ea, 0x61a710). Their header says the folder's number, not the
// name's.
TEST_F(GoldenTerrainAttributes, LoadsTheVariantsWithTheWorldOfTheirFolder)
{
    struct Variant
    {
        const char* path;
        std::uint8_t world;
    };
    constexpr std::array<Variant, 3> variants{{
        {"World31\\EncTerrain312.att", 31},
        {"World35\\EncTerrain351.att", 35},
        {"World35\\EncTerrain352.att", 35},
    }};
    for (const Variant& variant : variants)
    {
        const auto path = DataFile(variant.path);
        ASSERT_TRUE(path.has_value()) << variant.path;
        const std::optional<std::vector<char>> file = ReadFileBytes(*path);
        ASSERT_TRUE(file.has_value()) << variant.path;

        const auto loaded = LoadTerrainAttributes(std::as_bytes(std::span(*file)), variant.world);

        EXPECT_TRUE(loaded.HasValue()) << variant.path;
    }
}

// Lorencia's .att has two bytes per cell, which shows whether the loader keeps the right byte: of the 7,939 water
// cells (tile 5 of the .map's layer 1), 7,818 have attribute 4, the research's cross-check of both files
// (005f87b0-encterrain-map.md §8). The high byte, always zero in Lorencia, would give 0.
TEST_F(GoldenTerrainAttributes, LorenciaWaterHasAttribute4)
{
    const auto attributesPath = DataFile(AttributesFile(1));
    const auto mappingPath = DataFile("World1\\EncTerrain1.map");
    ASSERT_TRUE(attributesPath.has_value());
    ASSERT_TRUE(mappingPath.has_value());
    const std::optional<std::vector<char>> attributesFile = ReadFileBytes(*attributesPath);
    const std::optional<std::vector<char>> mappingFile = ReadFileBytes(*mappingPath);
    ASSERT_TRUE(attributesFile.has_value());
    ASSERT_TRUE(mappingFile.has_value());

    const auto attributes = LoadTerrainAttributes(std::as_bytes(std::span(*attributesFile)), 1);
    const auto mapping = LoadTerrainMapping(std::as_bytes(std::span(*mappingFile)), 1);

    ASSERT_TRUE(attributes.HasValue());
    ASSERT_TRUE(mapping.HasValue());
    constexpr std::uint8_t Water = 5;
    std::size_t water = 0;
    std::size_t waterWithAttribute4 = 0;
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        if (mapping.Value()->layer1[cell] == Water)
        {
            ++water;
            if (attributes.Value()->cells[cell] == 4)
            {
                ++waterWithAttribute4;
            }
        }
    }
    EXPECT_EQ(water, 7939u);
    EXPECT_EQ(waterWithAttribute4, 7818u);
}

// A cell the game changed after loading: where it is, what the file has and what the original's memory had.
struct ChangedCell
{
    std::uint8_t x;
    std::uint8_t y;
    std::uint16_t file;
    std::uint16_t memory;

    bool operator==(const ChangedCell&) const = default;
};

void PrintTo(const ChangedCell& cell, std::ostream* out)
{
    *out << "(" << int{cell.x} << ", " << int{cell.y} << "): " << cell.file << " -> " << cell.memory;
}

// The cells where MuRet's attributes differ from the dump `memory`, the WORDs at 0x07D0D348, in cell order, row by
// row. A cell missing from the dump counts as different, with 0 in memory.
std::vector<ChangedCell> CompareWithMemory(const TerrainAttributes& attributes, std::span<const std::byte> memory)
{
    ByteReader reader(memory);
    std::vector<ChangedCell> changed;
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        const std::uint16_t inMemory = reader.ReadU16Le().value_or(0);
        if (attributes.cells[cell] != inMemory)
        {
            const auto x = static_cast<std::uint8_t>(cell % TerrainSide);
            const auto y = static_cast<std::uint8_t>(cell / TerrainSide);
            changed.push_back({x, y, attributes.cells[cell], inMemory});
        }
    }
    return changed;
}

class MemoryTerrainAttributes : public Client::Tests::MemoryDumpTest
{
protected:
    // Fails the test if something is missing.
    std::optional<std::vector<ChangedCell>> CellsThatDiffer(const MemoryCapture& capture, std::string_view name) const
    {
        const auto path = DataFile(AttributesFile(capture.world));
        const auto dump = DumpFile(capture, name);
        if (!path || !dump)
        {
            return std::nullopt;
        }
        const std::optional<std::vector<char>> file = ReadFileBytes(*path);
        const std::optional<std::vector<char>> memory = ReadFileBytes(*dump);
        if (!file || !memory)
        {
            ADD_FAILURE() << "no se pudo leer " << (file ? *dump : *path);
            return std::nullopt;
        }
        if (memory->size() != TerrainCellCount * sizeof(std::uint16_t))
        {
            ADD_FAILURE() << name << " mide " << memory->size() << " bytes, no "
                          << TerrainCellCount * sizeof(std::uint16_t);
            return std::nullopt;
        }
        const auto loaded = LoadTerrainAttributes(std::as_bytes(std::span(*file)), capture.world);
        if (!loaded.HasValue())
        {
            ADD_FAILURE() << AttributesFile(capture.world) << " no cargó";
            return std::nullopt;
        }
        return CompareWithMemory(*loaded.Value(), std::as_bytes(std::span(*memory)));
    }
};

// The original main.exe's memory after loading the same file, in Devias (World3; s31_att.bin), but for 10 cells the
// game had already changed when the memory was read, as the research lists them (s31-assets/byte-a-byte.md): the
// character cell's bit 0x02, set at (199, 19) and cleared at (9, 79), and 0x04 cleared in two 4-cell passages.
TEST_F(MemoryTerrainAttributes, EqualsTheOriginalsMemoryInDeviasButForTheGameState)
{
    const auto changed = CellsThatDiffer(DeviasCapture, "s31_att.bin");

    ASSERT_TRUE(changed.has_value());
    const std::vector<ChangedCell> gameState = {
        {199, 19, 1, 3},  {9, 79, 3, 1},   {118, 115, 4, 0}, {119, 115, 4, 0}, {120, 115, 4, 0},
        {121, 115, 4, 0}, {92, 205, 4, 0}, {93, 205, 4, 0},  {94, 205, 4, 0},  {95, 205, 4, 0},
    };
    EXPECT_EQ(*changed, gameState);
}

// The same in Lorencia (World1; lor_att.bin), but for 7 cells (byte-a-byte.md, the second map): bit 0x02 set at
// (141, 117) and (130, 121) and cleared at (221, 11), and 0x04 cleared in the same 4 cells of (118..121, 115) as in
// Devias, though it is another map.
TEST_F(MemoryTerrainAttributes, EqualsTheOriginalsMemoryInLorenciaButForTheGameState)
{
    const auto changed = CellsThatDiffer(LorenciaCapture, "lor_att.bin");

    ASSERT_TRUE(changed.has_value());
    const std::vector<ChangedCell> gameState = {
        {221, 11, 2, 0},  {118, 115, 5, 1}, {119, 115, 5, 1}, {120, 115, 5, 1},
        {121, 115, 5, 1}, {141, 117, 1, 3}, {130, 121, 1, 3},
    };
    EXPECT_EQ(*changed, gameState);
}
} // namespace
