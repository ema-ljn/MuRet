#include "Data/WorldFolder.h"

#include <array>
#include <cstdint>
#include <string>

namespace Client::Data
{
namespace
{
// Un rango de mapas que comparte una carpeta.
struct FolderRange
{
    std::uint8_t first = 0;
    std::uint8_t last = 0;
    std::uint16_t folder = 0;
};

// 0x61a5e4..0x61a61d, en el orden en que compara: con signo, contra 11 y 17 (jl, jg), 24 y 29
// (jl, jle) o el 36 (jne), y 18 y 23 (jl, jg). Ninguno se pisa con otro, así que el orden no cambia
// el resultado.
constexpr std::array<FolderRange, 4> EventRanges = {{
    {.first = 11, .last = 17, .folder = 12},
    {.first = 24, .last = 29, .folder = 25},
    {.first = 36, .last = 36, .folder = 25},
    {.first = 18, .last = 23, .folder = 19},
}};

// 0x61a56c..0x61a57d: con el mapa 32, 0x61a560 pasa el mapa del juego (0x6c9dd0) a 9 antes de
// elegir la carpeta (Q-C58).
constexpr std::uint8_t MapWithTheFolderOfNine = 32;
constexpr std::uint8_t MapNine = 9;
} // namespace

std::uint16_t WorldFolderNumber(std::uint8_t map)
{
    if (map == MapWithTheFolderOfNine)
    {
        map = MapNine;
    }
    for (const FolderRange& range : EventRanges)
    {
        if (map >= range.first && map <= range.last)
        {
            return range.folder;
        }
    }
    return static_cast<std::uint16_t>(map + 1);
}

std::string WorldFolder(std::uint8_t map)
{
    return "World" + std::to_string(WorldFolderNumber(map));
}
} // namespace Client::Data
