#include "Data/WorldLoadIdentity.h"

#include <array>
#include <bit>
#include <cstdint>

namespace Client::Data
{
namespace
{
struct FolderRange
{
    std::int32_t first;
    std::int32_t last;
    std::int32_t folder;
};

// 05 § La carpeta de cada mapa, 0x61a5e4..0x61a61d: comparaciones signed después de 0x616ec0.
constexpr std::array<FolderRange, 4> EventRanges = {{
    {.first = 11, .last = 17, .folder = 12},
    {.first = 24, .last = 29, .folder = 25},
    {.first = 36, .last = 36, .folder = 25},
    {.first = 18, .last = 23, .folder = 19},
}};

// 05 § La carpeta de cada mapa, 0x61a56c..0x61a57d: ambos DWORD deben valer 32.
constexpr std::int32_t MapWithTheFolderOfNine = 32;
constexpr std::int32_t MapNine = 9;
} // namespace

std::int32_t MapAtWorldLoadStart(std::int32_t argumentMap, std::int32_t observedMap)
{
    if (argumentMap == MapWithTheFolderOfNine && observedMap == MapWithTheFolderOfNine)
    {
        return MapNine;
    }
    return observedMap;
}

std::int32_t WorldFolderAfterModels(std::int32_t observedMap)
{
    for (const FolderRange& range : EventRanges)
    {
        if (observedMap >= range.first && observedMap <= range.last)
        {
            return range.folder;
        }
    }
    // 0x61a5ec: el incremento DWORD conserva el wrap; no es una suma signed de C++.
    const std::uint32_t folder = std::bit_cast<std::uint32_t>(observedMap) + std::uint32_t{1};
    return std::bit_cast<std::int32_t>(folder);
}
} // namespace Client::Data
