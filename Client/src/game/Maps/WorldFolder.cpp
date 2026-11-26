#include "Maps/WorldFolder.h"

#include "Maps/WorldLoadIdentity.h"

#include <cstdint>
#include <string>

namespace Client::Maps
{
std::uint16_t WorldFolderNumber(std::uint8_t map)
{
    // A stable byte projection: the result is between 1 and 256, with no loss when narrowing.
    const std::int32_t effectiveMap = MapAtWorldLoadStart(map, map);
    return static_cast<std::uint16_t>(WorldFolderAfterModels(effectiveMap));
}

std::string WorldFolder(std::uint8_t map)
{
    return "World" + std::to_string(WorldFolderNumber(map));
}
} // namespace Client::Maps
