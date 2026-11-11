#include "World/WorldTerrain.h"

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/DataFile.h"
#include "Data/DataPath.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainLight.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Data/Textures/Sampling.h"
#include "Data/WorldFolder.h"
#include "World/TerrainCells.h"
#include "World/KalimaWorld.h"
#include "World/TerrainLighting.h"
#include "World/WorldFiles.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <system_error>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::World
{
namespace
{
// Los tiles de 0x61a560, en sus slots 0x23..0x30.
constexpr std::array<std::string_view, 14> TileNames{
    "TileGrass01", "TileGrass02", "TileGround01", "TileGround02", "TileGround03", "TileWater01", "TileWood01",
    "TileRock01",  "TileRock02",  "TileRock03",   "TileRock04",   "TileRock05",   "TileRock06",  "TileRock07",
};
constexpr std::uint16_t FirstTile = 0x23;
// Los nombres del pasto (0x6d1174, 0x6d1160 y 0x6d114c: "%s\\TileGrass0k.tga").
constexpr std::array<std::string_view, GrassTextureCount> GrassNames{"TileGrass01", "TileGrass02", "TileGrass03"};
// 0x63b870(…, 0x2600, 0x2901, …): GL_NEAREST y GL_REPEAT.
constexpr Data::Textures::Sampling TileSampling{Data::Textures::Filter::Nearest, Data::Textures::Wrap::Repeat};

// Los mapas que arman sus tiles en 0x4377a0: del 0x18 al 0x1d y el 0x24.
bool HasOwnTiles(std::uint8_t map)
{
    return (map >= 0x18 && map <= 0x1d) || map == 0x24;
}

std::string FileError(const std::string& relative, Data::DataError error)
{
    return "Data\\" + relative + ": " + std::string(Data::DescribeDataError(error));
}

enum class TerrainFileKind
{
    Mapping,
    Attributes,
    Heights,
    Light
};

// Distingue el fopen fallido del fread/parse inválido: sólo el primero pide "file not found".
bool CannotOpen(const std::filesystem::path& dataRoot, const std::string& relative)
{
    std::error_code error;
    const auto path = Data::ResolveDataPath(dataRoot, relative, error);
    if (!path)
        return true;
    const std::ifstream file(*path, std::ios::binary);
    return !file.is_open();
}

void RememberFatal(std::optional<std::string>& fatalMessage, std::string message)
{
    // WM_DESTROY ya fue pedido: ninguna carga posterior debe borrar ni reemplazar su aviso.
    if (!fatalMessage)
        fatalMessage = std::move(message);
}

void FileParseFailed(TerrainFileKind kind, const std::string& relative, Data::DataError error,
                     std::optional<std::string>& fatalMessage)
{
    // 61a65c/61a75d y 5f8588..5f8617. UnexpectedSize del ATT devuelve -1 sin aviso.
    if (error == Data::DataError::WrongWorld)
        RememberFatal(fatalMessage, "Data\\" + relative + " file corrupted.");
    else if (kind == TerrainFileKind::Attributes && error == Data::DataError::InvalidData)
        RememberFatal(fatalMessage, "Data error");
}

template <typename T, typename Load>
Core::Expected<std::unique_ptr<T>, std::string>
LoadFile(const std::filesystem::path& dataRoot, const std::string& relative, TerrainFileKind kind,
         std::optional<std::string>& fatalMessage, Load load, bool* failedToOpen = nullptr)
{
    const Core::Expected<std::vector<char>, std::string> file = ReadWorldFile(dataRoot, relative);
    if (!file.HasValue())
    {
        const bool cannotOpen = CannotOpen(dataRoot, relative);
        if (failedToOpen)
            *failedToOpen = cannotOpen;
        if (cannotOpen && kind != TerrainFileKind::Mapping)
        {
            const std::string suffix = kind == TerrainFileKind::Light ? " - File not exist." : " file not found.";
            RememberFatal(fatalMessage, "Data\\" + relative + suffix);
        }
        return Core::Unexpected(file.Error());
    }
    auto loaded = load(std::as_bytes(std::span(file.Value())));
    if (!loaded.HasValue())
    {
        FileParseFailed(kind, relative, loaded.Error(), fatalMessage);
        return Core::Unexpected(FileError(relative, loaded.Error()));
    }
    return std::move(loaded).Value();
}
} // namespace

Core::Expected<void, std::string> LoadWorldTerrainLayout(const std::filesystem::path& dataRoot, std::uint8_t map,
                                                         WorldTerrain& terrain,
                                                         std::optional<std::string>& fatalMessage)
{
    const std::string folder = Data::WorldFolder(map) + "\\";
    const auto world = static_cast<std::uint8_t>(Data::WorldFolderNumber(map));
    const std::string number = std::to_string(world);
    bool mappingCannotOpen = false;
    auto mapping = LoadFile<Data::Terrain::TerrainMapping>(
        dataRoot, folder + "EncTerrain" + number + ".map", TerrainFileKind::Mapping, fatalMessage,
        [world](std::span<const std::byte> bytes) { return Data::Terrain::LoadTerrainMapping(bytes, world); },
        &mappingCannotOpen);
    if (!mapping.HasValue() && !mappingCannotOpen)
        return Core::Unexpected(mapping.Error());
    if (mapping.HasValue())
        terrain.mapping = std::move(mapping).Value();
    auto attributes = LoadFile<Data::Terrain::TerrainAttributes>(
        dataRoot, folder + "EncTerrain" + number + ".att", TerrainFileKind::Attributes, fatalMessage,
        [world](std::span<const std::byte> bytes) { return Data::Terrain::LoadTerrainAttributes(bytes, world); });
    if (!attributes.HasValue())
        return Core::Unexpected(attributes.Error());
    terrain.attributes = std::move(attributes).Value();
    // Q-C27: el original continúa con defaults si el MAP no abre. MuRet llega al ATT fatal,
    // pero si éste existe conserva el diagnóstico del MAP; nunca fabrica un terreno utilizable.
    if (mappingCannotOpen)
        return Core::Unexpected(mapping.Error());
    return {};
}

Core::Expected<void, std::string> LoadWorldTerrainSurface(const std::filesystem::path& dataRoot, std::uint8_t map,
                                                          WorldTerrain& terrain,
                                                          std::optional<std::string>& fatalMessage)
{
    const std::string folder = Data::WorldFolder(map) + "\\";
    const auto world = static_cast<std::uint8_t>(Data::WorldFolderNumber(map));
    auto heights = LoadFile<Data::Terrain::TerrainHeights>(
        dataRoot, folder + "TerrainHeight.OZB", TerrainFileKind::Heights, fatalMessage,
        [world](std::span<const std::byte> bytes) { return Data::Terrain::LoadTerrainHeights(bytes, world); });
    if (!heights.HasValue())
        return Core::Unexpected(heights.Error());
    terrain.heights = std::move(heights).Value();
    auto light = LoadFile<Data::Terrain::TerrainLight>(dataRoot, folder + "TerrainLight.OZJ", TerrainFileKind::Light,
                                                       fatalMessage, [](std::span<const std::byte> bytes)
                                                       { return Data::Terrain::LoadTerrainLight(bytes); });
    if (!light.HasValue())
        return Core::Unexpected(light.Error());
    terrain.lighting = ShadeTerrainLight(*light.Value(), *terrain.heights, map);
    return {};
}

Core::Expected<WorldTerrain, std::string> LoadWorldTerrain(const std::filesystem::path& dataRoot, std::uint8_t map)
{
    WorldTerrain terrain;
    std::optional<std::string> fatalMessage;
    if (auto layout = LoadWorldTerrainLayout(dataRoot, map, terrain, fatalMessage); !layout.HasValue())
        return Core::Unexpected(layout.Error());
    if (auto surface = LoadWorldTerrainSurface(dataRoot, map, terrain, fatalMessage); !surface.HasValue())
        return Core::Unexpected(surface.Error());
    return terrain;
}

Core::Expected<std::vector<Data::Terrain::TerrainObject>, std::string>
LoadWorldObjects(const std::filesystem::path& dataRoot, std::uint8_t map, std::optional<std::string>* fatalMessage)
{
    const auto world = static_cast<std::uint8_t>(Data::WorldFolderNumber(map));
    const std::string relative = Data::WorldFolder(map) + "\\EncTerrain" + std::to_string(world) + ".obj";
    const Core::Expected<std::vector<char>, std::string> file = ReadWorldFile(dataRoot, relative);
    if (!file.HasValue())
    {
        // 607395..6073ca: el OBJ ausente también avisa antes de WM_DESTROY.
        if (fatalMessage && CannotOpen(dataRoot, relative))
            RememberFatal(*fatalMessage, "Data\\" + relative + " file not found.");
        return Core::Unexpected(file.Error());
    }
    auto objects = Data::Terrain::LoadTerrainObjects(std::as_bytes(std::span(file.Value())), world);
    if (!objects.HasValue())
    {
        if (fatalMessage && objects.Error() == Data::DataError::WrongWorld)
            RememberFatal(*fatalMessage, "Data\\" + relative + " file corrupted.");
        return Core::Unexpected(FileError(relative, objects.Error()));
    }
    return std::move(objects).Value();
}

std::vector<WorldTileTexture> WorldTileTextures(std::uint8_t map)
{
    std::vector<WorldTileTexture> tiles;
    if (HasOwnTiles(map))
    {
        return tiles;
    }
    const std::string folder = Data::WorldFolder(map) + "\\";
    for (std::size_t tile = 0; tile < TileNames.size(); ++tile)
    {
        tiles.push_back(WorldTileTexture{.path = folder + std::string(TileNames[tile]) + ".jpg",
                                         .slot = static_cast<std::uint16_t>(FirstTile + tile),
                                         .sampling = TileSampling,
                                         .required = tile == 0});
    }
    return tiles;
}

std::vector<WorldTileTexture> WorldGrassTextures(std::uint8_t map)
{
    std::vector<WorldTileTexture> grass;
    if (HasOwnTiles(map))
    {
        return grass;
    }
    const std::string folder = Data::WorldFolder(map) + "\\";
    for (std::size_t texture = 0; texture < GrassNames.size(); ++texture)
    {
        // 0x63bd00(…, 0x32 + k, 0x2600, 0x2901, 0, 0): el sexto argumento en 0, no avisa.
        grass.push_back(WorldTileTexture{.path = folder + std::string(GrassNames[texture]) + ".tga",
                                         .slot = static_cast<std::uint16_t>(FirstGrassSlot + texture),
                                         .sampling = TileSampling,
                                         .required = false});
    }
    return grass;
}

std::vector<WorldTileTexture> WorldWeatherTextures(std::uint8_t map)
{
    if (HasOwnTiles(map))
        return {};
    // 61ac64/61ac92 cargan primero la TGA y después la JPG sobre el MISMO slot64.
    // Todos llevan sexto argumento0: faltar o fallar al leer no elimina la alternativa anterior.
    constexpr Data::Textures::Sampling sampling{Data::Textures::Filter::Nearest, Data::Textures::Wrap::Clamp};
    constexpr std::uint8_t CrywolfMap = 34;
    const std::string folder = Data::WorldFolder(map) + "\\";
    return {{folder + "leaf01.tga", 0x64, sampling, false},
            {folder + "leaf01.jpg", 0x64, sampling, false},
            {folder + "leaf02.jpg", 0x65, sampling, false},
            {map == CrywolfMap ? "World1\\rain011.tga" : "World1\\rain01.tga", 0x66, sampling, false},
            {"World1\\rain02.tga", 0x67, sampling, false},
            {"World10\\rain03.tga", 0x68, sampling, false}};
}

WorldTileTexture AcceptBoxTexture()
{
    // 0x61ae60: 0x63b870("Interface\\accept_box01.jpg", 0x122, 0x2600, 0x2900, 0, 1).
    return WorldTileTexture{.path = "Interface\\accept_box01.jpg",
                            .slot = 0x122,
                            .sampling = {Data::Textures::Filter::Nearest, Data::Textures::Wrap::Clamp},
                            .required = true};
}

std::vector<WorldTileTexture> KalimaObjectTextures(std::uint8_t map)
{
    if (!IsKalimaMap(map))
        return {};
    constexpr std::uint16_t Count = 32;
    constexpr std::uint16_t FirstSlot = 65;
    constexpr Data::Textures::Sampling Sampling{Data::Textures::Filter::Linear, Data::Textures::Wrap::Repeat};
    std::vector<WorldTileTexture> textures;
    textures.reserve(Count);
    for (std::uint16_t index = 0; index < Count; ++index)
    {
        const std::string number = (index < 10 ? "0" : "") + std::to_string(index);
        textures.push_back(
            {"Object8\\wt" + number + ".jpg", static_cast<std::uint16_t>(FirstSlot + index), Sampling, false});
    }
    return textures;
}
} // namespace Client::World
