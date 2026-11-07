#include "Data/WorldFolder.h"

#include "Data/WorldLoadIdentity.h"

#include <cstdint>
#include <string>

namespace Client::Data
{
std::uint16_t WorldFolderNumber(std::uint8_t map)
{
    // Proyección byte estable: el resultado está entre 1 y 256, sin pérdida al estrechar.
    const std::int32_t effectiveMap = MapAtWorldLoadStart(map, map);
    return static_cast<std::uint16_t>(WorldFolderAfterModels(effectiveMap));
}

std::string WorldFolder(std::uint8_t map)
{
    return "World" + std::to_string(WorldFolderNumber(map));
}
} // namespace Client::Data
