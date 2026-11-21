#include "Scenes/MapNames.h"

#include <cstddef>
#include <cstdint>

namespace Client::Scenes
{
std::size_t MapNameText(std::int32_t map)
{
    constexpr std::int32_t FirstMapText = 30;
    if (map == 0x22)
        return 1851;
    if (map == 0x21)
        return 1850;
    if (map == 0x1e)
        return 669;
    if (map == 0x1f)
        return 59;
    if (map >= 0x12 && map <= 0x17)
        return 57;
    if ((map >= 0x18 && map <= 0x1d) || map == 0x24)
        return 58;
    if (map >= 0x0b && map <= 0x11)
        return 56;
    if (map == 0x0a)
        return 55;
    if (map == 0x20)
        return 39;
    return static_cast<std::size_t>(FirstMapText + map);
}
} // namespace Client::Scenes
