#pragma once

#include <cstdint>

namespace Client::Maps
{
inline constexpr std::int32_t FirstBloodCastleMap = 11;
inline constexpr std::int32_t LastBloodCastleMap = 17;
inline constexpr std::int32_t FirstChaosCastleMap = 18;
inline constexpr std::int32_t LastChaosCastleMap = 23;
inline constexpr std::int32_t FirstKalimaMap = 24;
inline constexpr std::int32_t LastKalimaMap = 29;
inline constexpr std::int32_t SeventhKalimaMap = 36;

[[nodiscard]] constexpr bool IsBloodCastle(std::int32_t map)
{
    return map >= FirstBloodCastleMap && map <= LastBloodCastleMap;
}

[[nodiscard]] constexpr bool IsChaosCastle(std::int32_t map)
{
    return map >= FirstChaosCastleMap && map <= LastChaosCastleMap;
}

[[nodiscard]] constexpr bool IsKalimaMap(std::int32_t map)
{
    return (map >= FirstKalimaMap && map <= LastKalimaMap) || map == SeventhKalimaMap;
}
} // namespace Client::Maps
