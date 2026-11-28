#include "CastleSiege/CastleSiegeMap.h"

#include "Core/Expected.h"
#include "Maps/MapId.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Terrain/WorldTerrain.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Client::CastleSiege
{
Core::Expected<void, std::string> SetTheSiegeActive(std::uint8_t value, Kernel::WorldState& world,
                                                    Terrain::WorldTerrain* terrain,
                                                    const std::filesystem::path& dataRoot)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    runtime.castleFlags.previousSiegeActive = runtime.castleFlags.siegeActive;
    runtime.castleFlags.siegeActive = value != 0;
    constexpr std::int32_t CastleMap = Maps::CastleSiegeMap;
    if (runtime.map != CastleMap)
        return {};
    if (terrain == nullptr)
        return Core::Unexpected(std::string("0x4294c0: no hay terreno para los archivos del asedio"));
    constexpr auto Folder = "World31";
    constexpr std::int32_t PeaceAttributes = 31;
    constexpr std::int32_t WarAttributes = 312;
    const auto attributes = Terrain::ReloadWorldTerrainAttributes(
        dataRoot, Folder, runtime.castleFlags.siegeActive ? WarAttributes : PeaceAttributes, *terrain,
        runtime.fatalDataMessage);
    const auto light = Terrain::ReloadWorldTerrainLight(
        dataRoot, Folder, runtime.castleFlags.siegeActive ? "TerrainLight2" : "TerrainLight", CastleMap, *terrain,
        runtime.fatalDataMessage);
    runtime.castleFlags.siegeMusicTime = 0.0f;
    if (!attributes.HasValue())
        return attributes;
    return light;
}
} // namespace Client::CastleSiege
