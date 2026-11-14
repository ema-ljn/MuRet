#include "Scenes/ZenText.h"

#include <cstdint>
#include <format>
#include <string>

namespace Client::Scenes
{
std::string FormatZen(std::uint32_t zen)
{
    const std::uint32_t units = zen % 1000u;
    const std::uint32_t thousands = zen % 1000000u / 1000u;
    const std::uint32_t millions = zen % 1000000000u / 1000000u;
    if (zen >= 1000000000u)
        return std::format("{},{:03},{:03},{:03}", zen / 1000000000u, millions, thousands, units);
    if (zen >= 1000000u)
        return std::format("{},{:03},{:03}", millions, thousands, units);
    if (zen >= 1000u)
        return std::format("{},{:03}", thousands, units);
    return std::format("{}", units);
}

std::uint32_t ZenColor(std::uint32_t zen)
{
    if (zen >= 10000000u)
        return 0xff0000ffu;
    if (zen >= 1000000u)
        return 0xff0096ffu;
    if (zen >= 100000u)
        return 0xff18c900u;
    return 0xff96dcffu;
}
} // namespace Client::Scenes
