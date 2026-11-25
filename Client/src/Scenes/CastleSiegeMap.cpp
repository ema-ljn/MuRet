#include "Scenes/CastleSiegeMap.h"

#include "Core/Expected.h"
#include "Maps/MapId.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/WorldTerrain.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Client::Scenes
{
Core::Expected<void, std::string> SetTheSiegeActive(std::uint8_t value, WorldState& world,
                                                    const std::filesystem::path& dataRoot)
{
    WorldRuntime& runtime = world.Runtime();
    runtime.previousSiegeActive = runtime.siegeActive;
    runtime.siegeActive = value != 0;
    constexpr std::int32_t CastleMap = Maps::CastleSiegeMap;
    if (runtime.map != CastleMap)
        return {};
    auto& terrain = world.Map().terrain;
    constexpr auto Folder = "World31";
    constexpr std::int32_t PeaceAttributes = 31;
    constexpr std::int32_t WarAttributes = 312;
    const auto attributes = World::ReloadWorldTerrainAttributes(
        dataRoot, Folder, runtime.siegeActive ? WarAttributes : PeaceAttributes, terrain, runtime.fatalDataMessage);
    const auto light =
        World::ReloadWorldTerrainLight(dataRoot, Folder, runtime.siegeActive ? "TerrainLight2" : "TerrainLight",
                                       CastleMap, terrain, runtime.fatalDataMessage);
    runtime.siegeMusicTime = 0.0f;
    if (!attributes.HasValue())
        return attributes;
    return light;
}
} // namespace Client::Scenes
