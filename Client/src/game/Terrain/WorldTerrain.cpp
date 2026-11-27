#include "Terrain/WorldTerrain.h"

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
#include "Data/Textures/TextureSet.h"
#include "Maps/WorldFolder.h"
#include "Maps/MapId.h"
#include "Terrain/TerrainCells.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/WorldFiles.h"

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

namespace Client::Terrain
{
namespace
{
// The tiles of 0x61a560, in their slots 0x23..0x30.
constexpr std::array<std::string_view, 14> TileNames{
    "TileGrass01", "TileGrass02", "TileGround01", "TileGround02", "TileGround03", "TileWater01", "TileWood01",
    "TileRock01",  "TileRock02",  "TileRock03",   "TileRock04",   "TileRock05",   "TileRock06",  "TileRock07",
};
constexpr std::uint16_t FirstTile = 0x23;
// The grass names (0x6d1174, 0x6d1160 and 0x6d114c: "%s\\TileGrass0k.tga").
constexpr std::array<std::string_view, GrassTextureCount> GrassNames{"TileGrass01", "TileGrass02", "TileGrass03"};
// 0x63b870(…, 0x2600, 0x2901, …): GL_NEAREST and GL_REPEAT.
constexpr Data::Textures::Sampling TileSampling{Data::Textures::Filter::Nearest, Data::Textures::Wrap::Repeat};

// The maps that build their tiles in 0x4377a0: 0x18 to 0x1d and 0x24.
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

// Tells a failed fopen from an invalid fread/parse: only the first asks for "file not found".
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
    // WM_DESTROY was already requested: no later load may erase or replace its notice.
    if (!fatalMessage)
        fatalMessage = std::move(message);
}

void FileParseFailed(TerrainFileKind kind, const std::string& relative, Data::DataError error,
                     std::optional<std::string>& fatalMessage)
{
    // 0x61a65c/0x61a75d and 0x5f8588..0x5f8617. The ATT's UnexpectedSize returns -1 without a notice.
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
                                                         std::optional<std::string>& fatalMessage,
                                                         std::optional<std::int32_t> attributesNumber,
                                                         bool reuseMappingForAttributes)
{
    const std::string folder = Maps::WorldFolder(map) + "\\";
    const auto world = static_cast<std::uint8_t>(Maps::WorldFolderNumber(map));
    const std::string number = std::to_string(world);
    const std::string attributesFile = std::to_string(attributesNumber.value_or(world));
    const std::string mappingPath = folder + "EncTerrain" + number + ".map";
    bool mappingCannotOpen = false;
    auto mapping = LoadFile<Data::Terrain::TerrainMapping>(
        dataRoot, mappingPath, TerrainFileKind::Mapping, fatalMessage, [world](std::span<const std::byte> bytes)
        { return Data::Terrain::LoadTerrainMapping(bytes, world); }, &mappingCannotOpen);
    if (!mapping.HasValue() && !mappingCannotOpen)
        return Core::Unexpected(mapping.Error());
    if (mapping.HasValue())
        terrain.mapping = std::move(mapping).Value();
    const std::string attributesPath =
        reuseMappingForAttributes ? mappingPath : folder + "EncTerrain" + attributesFile + ".att";
    auto attributes = LoadFile<Data::Terrain::TerrainAttributes>(
        dataRoot, attributesPath, TerrainFileKind::Attributes, fatalMessage,
        [world, reuseMappingForAttributes](std::span<const std::byte> bytes)
            -> Core::Expected<std::unique_ptr<Data::Terrain::TerrainAttributes>, Data::DataError>
        {
            auto decoded = Data::Terrain::LoadTerrainAttributes(bytes, world);
            // 0x5f84fc returns −1 before writing the table. 0x61a760 goes on to the OBJ.
            if (reuseMappingForAttributes && !decoded.HasValue() && decoded.Error() == Data::DataError::UnexpectedSize)
                return std::unique_ptr<Data::Terrain::TerrainAttributes>{};
            return decoded;
        });
    if (!attributes.HasValue())
        return Core::Unexpected(attributes.Error());
    if (attributes.Value())
        terrain.attributes = std::move(attributes).Value();
    else if (!terrain.attributes)
        terrain.attributes = std::make_unique<Data::Terrain::TerrainAttributes>();
    // Q-C27: the original goes on with defaults if the MAP does not open. MuRet reaches the fatal ATT, but if the ATT
    // exists it keeps the MAP's diagnostic; it never fabricates a usable terrain.
    if (mappingCannotOpen)
        return Core::Unexpected(mapping.Error());
    return {};
}

Core::Expected<void, std::string> LoadWorldTerrainSurface(const std::filesystem::path& dataRoot, std::uint8_t map,
                                                          WorldTerrain& terrain,
                                                          std::optional<std::string>& fatalMessage,
                                                          std::string_view lightName, bool retainLightOnFailure)
{
    const std::string folder = Maps::WorldFolder(map) + "\\";
    const auto world = static_cast<std::uint8_t>(Maps::WorldFolderNumber(map));
    auto heights = LoadFile<Data::Terrain::TerrainHeights>(
        dataRoot, folder + "TerrainHeight.OZB", TerrainFileKind::Heights, fatalMessage,
        [world](std::span<const std::byte> bytes) { return Data::Terrain::LoadTerrainHeights(bytes, world); });
    if (!heights.HasValue())
        return Core::Unexpected(heights.Error());
    terrain.heights = std::move(heights).Value();
    bool lightCannotOpen = false;
    auto light = LoadFile<Data::Terrain::TerrainLight>(
        dataRoot, folder + std::string(lightName) + ".OZJ", TerrainFileKind::Light, fatalMessage,
        [](std::span<const std::byte> bytes) { return Data::Terrain::LoadTerrainLight(bytes); }, &lightCannotOpen);
    if (!light.HasValue() && !retainLightOnFailure)
        return Core::Unexpected(light.Error());
    if (light.HasValue())
        terrain.rawLight = std::move(light).Value();
    else if (!terrain.rawLight)
        terrain.rawLight = std::make_unique<Data::Terrain::TerrainLight>();
    // 0x5f8f40 shades the retained light with the heights just loaded.
    terrain.lighting = ShadeTerrainLight(*terrain.rawLight, *terrain.heights, map);
    if (lightCannotOpen)
        return Core::Unexpected(light.Error());
    return {};
}

Core::Expected<void, std::string> ReloadWorldTerrainLight(const std::filesystem::path& dataRoot,
                                                          const std::string& folder, std::string_view lightName,
                                                          std::uint8_t map, WorldTerrain& terrain,
                                                          std::optional<std::string>& fatalMessage)
{
    const std::string relative = folder + "\\" + std::string(lightName) + ".OZJ";
    if (!terrain.heights)
        return Core::Unexpected("0x5f8b10: la luz de Data\\" + relative +
                                " se sombrea con las alturas del mundo, que "
                                "no están cargadas");
    auto light = LoadFile<Data::Terrain::TerrainLight>(dataRoot, relative, TerrainFileKind::Light, fatalMessage,
                                                       [](std::span<const std::byte> bytes)
                                                       { return Data::Terrain::LoadTerrainLight(bytes); });
    // 0x5f8f4c: 0x5f8f40 does not check what 0x63b490 returns; without a new light the previous one stays, shaded the
    // same again.
    if (!light.HasValue())
    {
        if (terrain.rawLight)
            terrain.lighting = ShadeTerrainLight(*terrain.rawLight, *terrain.heights, map);
        return {};
    }
    terrain.rawLight = std::move(light).Value();
    terrain.lighting = ShadeTerrainLight(*terrain.rawLight, *terrain.heights, map);
    return {};
}

Core::Expected<void, std::string> ReloadWorldTerrainAttributes(const std::filesystem::path& dataRoot,
                                                               const std::string& folder, std::int32_t number,
                                                               WorldTerrain& terrain,
                                                               std::optional<std::string>& fatalMessage)
{
    const std::string relative = folder + "\\EncTerrain" + std::to_string(number) + ".att";
    auto attributes = LoadFile<Data::Terrain::TerrainAttributes>(
        dataRoot, relative, TerrainFileKind::Attributes, fatalMessage,
        [](std::span<const std::byte> bytes) { return Data::Terrain::LoadTerrainAttributesOfAnyWorld(bytes); });
    // 0x42ffe8: BD 00 does not check what 0x5f8390 returns; the fatal notices already stand.
    if (!attributes.HasValue())
        return {};
    terrain.attributes = std::move(attributes).Value();
    return {};
}

const WorldTerrain& EmptyTerrain()
{
    static const WorldTerrain Empty{};
    return Empty;
}

const WorldTerrain& TerrainOrEmpty(const WorldTerrain* terrain)
{
    return terrain != nullptr ? *terrain : EmptyTerrain();
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
    const auto world = static_cast<std::uint8_t>(Maps::WorldFolderNumber(map));
    const std::string relative = Maps::WorldFolder(map) + "\\EncTerrain" + std::to_string(world) + ".obj";
    const Core::Expected<std::vector<char>, std::string> file = ReadWorldFile(dataRoot, relative);
    if (!file.HasValue())
    {
        // 0x607395..0x6073ca: a missing OBJ also warns before WM_DESTROY.
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
    const std::string folder = Maps::WorldFolder(map) + "\\";
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
    const std::string folder = Maps::WorldFolder(map) + "\\";
    for (std::size_t texture = 0; texture < GrassNames.size(); ++texture)
    {
        // 0x63bd00(…, 0x32 + k, 0x2600, 0x2901, 0, 0): the sixth argument at 0, it does not warn.
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
    // 0x61ac64/0x61ac92 load the TGA first and then the JPG into the SAME slot 0x64. All take a sixth argument of 0:
    // missing or failing to read does not remove the earlier alternative.
    constexpr Data::Textures::Sampling sampling{Data::Textures::Filter::Nearest, Data::Textures::Wrap::Clamp};
    constexpr std::uint8_t CrywolfMap = Maps::CrywolfMap;
    const std::string folder = Maps::WorldFolder(map) + "\\";
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

WorldTileTexture DropTexture()
{
    // 0x63b870("Object8\\drop01.jpg", 0x607, 0x2600, 0x2900, 0, 1).
    return WorldTileTexture{.path = "Object8\\drop01.jpg",
                            .slot = 0x607,
                            .sampling = {Data::Textures::Filter::Nearest, Data::Textures::Wrap::Clamp},
                            .required = true};
}

std::vector<WorldTileTexture> CausticTextures(std::uint8_t /*map*/)
{
    // 0x617753..0x617788 on map 7; 0x618dd1..0x618e29 on the others, only once and in the same order. The caller
    // 0x61a560 receives the network's byte map or 54/55 from the scene, never the DWORD sentinel −1.
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

WorldTileTexture ObjectLightTexture()
{
    return {.path = "Object8\\light01.jpg",
            .slot = 0x68,
            .sampling = {Data::Textures::Filter::Linear, Data::Textures::Wrap::Repeat},
            .required = false};
}

std::vector<WorldTileTexture> TarkanObjectTextures(std::uint8_t map)
{
    constexpr std::uint8_t TarkanMap = Maps::TarkanMap;
    if (map != TarkanMap)
        return {};
    constexpr Data::Textures::Sampling Repeat{Data::Textures::Filter::Linear, Data::Textures::Wrap::Repeat};
    constexpr Data::Textures::Sampling Clamp{Data::Textures::Filter::Linear, Data::Textures::Wrap::Clamp};
    return {{"Object9\\sand01.jpg", 0x5c2, Repeat, true},
            {"Object9\\sand02.jpg", 0x5c3, Repeat, true},
            {"Object9\\Impack03.jpg", 0x716, Clamp, true}};
}

std::vector<WorldTileTexture> IcarusObjectTextures(std::uint8_t map)
{
    constexpr std::uint8_t IcarusMap = Maps::IcarusMap;
    if (map != IcarusMap)
        return {};
    constexpr Data::Textures::Sampling Clamp{Data::Textures::Filter::Linear, Data::Textures::Wrap::Clamp};
    return {{"Effect\\cloudLight.jpg", 0x623, Clamp, true}};
}
std::span<const Data::Textures::TextureSetEntry> KalimaWaterTextureSet()
{
    static constexpr std::array<Data::Textures::TextureSetEntry, 2> Textures{{
        {"Object25\\water1.tga", 0x23, {Data::Textures::Filter::Linear, Data::Textures::Wrap::Repeat}},
        {"Object25\\water2.jpg", 0x24, {Data::Textures::Filter::Nearest, Data::Textures::Wrap::Clamp}},
    }};
    return Textures;
}

std::vector<WorldTileTexture> CastleSiegeTextures(std::uint8_t map)
{
    constexpr std::uint8_t CastleMap = Maps::CastleSiegeMap;
    if (map != CastleMap)
        return {};
    constexpr Data::Textures::Sampling Clamp{Data::Textures::Filter::Linear, Data::Textures::Wrap::Clamp};
    return {{"World31\\Map1.jpg", 0x69d, Clamp, true}, {"World31\\Map2.jpg", 0x69e, Clamp, true}};
}
} // namespace Client::Terrain
