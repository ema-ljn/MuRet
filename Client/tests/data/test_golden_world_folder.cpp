#include "Data/WorldFolder.h"

#include "Data/DataPath.h"
#include "support/GoldenDir.h"
#include "support/TerrainFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

// Las carpetas de WorldFolder contra el Data\ del laboratorio (Client/tests/support/GoldenDir.h):
// cada mundo que trae es la carpeta de algún mapa, y cada mapa de OriginalMaps encuentra su
// terreno, salvo el que el laboratorio no trae.
namespace
{
using Client::Data::OriginalMaps;
using Client::Data::ResolveDataPath;
using Client::Data::WorldFolder;
using Client::Data::WorldFolderNumber;
using Client::Tests::LabWorlds;

using GoldenWorldFolder = Client::Tests::GoldenDataTest;

// El único mapa de OriginalMaps cuya carpeta no está en el laboratorio: el 35, World36 (09).
constexpr std::uint8_t MapWithoutLabWorld = 35;

// La ruta que pide 0x61a560 para el terreno: "Data\%s\EncTerrain%d.map" (0x6d12e8), con la carpeta
// y el mismo N.
std::string TerrainFile(std::uint8_t map)
{
    return WorldFolder(map) + "\\EncTerrain" + std::to_string(WorldFolderNumber(map)) + ".map";
}

TEST_F(GoldenWorldFolder, EveryLabWorldIsTheFolderOfAMap)
{
    for (const std::uint8_t world : LabWorlds)
    {
        const bool someMap = std::any_of(OriginalMaps.begin(), OriginalMaps.end(),
                                         [world](std::uint8_t map) { return WorldFolderNumber(map) == world; });
        EXPECT_TRUE(someMap) << "World" << int{world} << " no es la carpeta de ningún mapa";
        EXPECT_TRUE(DataFile("World" + std::to_string(world)).has_value()) << "World" << int{world};
    }
}

TEST_F(GoldenWorldFolder, EveryMapFindsItsTerrain)
{
    std::vector<std::uint8_t> missing;
    for (const std::uint8_t map : OriginalMaps)
    {
        std::error_code error;
        const std::optional<std::filesystem::path> path = ResolveDataPath(m_dataRoot, TerrainFile(map), error);
        ASSERT_FALSE(error) << "mapa " << int{map} << ": " << error.message();
        if (!path.has_value())
        {
            missing.push_back(map);
        }
    }

    // Si el laboratorio suma World36, o le falta otro, el test lo dice.
    const std::vector<std::uint8_t> expected{MapWithoutLabWorld};
    EXPECT_EQ(missing, expected);
}
} // namespace
