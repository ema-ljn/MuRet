#pragma once

#include <cstdint>

namespace Client::Maps
{
inline constexpr std::int32_t LorenciaMap = 0;
inline constexpr std::int32_t DungeonMap = 1;
inline constexpr std::int32_t DeviasMap = 2;
inline constexpr std::int32_t NoriaMap = 3;
inline constexpr std::int32_t LostTowerMap = 4;
inline constexpr std::int32_t ExileMap = 5;
inline constexpr std::int32_t ArenaMap = 6;
inline constexpr std::int32_t AtlansMap = 7;
inline constexpr std::int32_t TarkanMap = 8;
inline constexpr std::int32_t DevilSquareMap = 9;
inline constexpr std::int32_t IcarusMap = 10;
inline constexpr std::int32_t FirstBloodCastleMap = 11;
inline constexpr std::int32_t LastBloodCastleMap = 17;
inline constexpr std::int32_t FirstChaosCastleMap = 18;
inline constexpr std::int32_t LastChaosCastleMap = 23;
inline constexpr std::int32_t FirstKalimaMap = 24;
inline constexpr std::int32_t LastKalimaMap = 29;
inline constexpr std::int32_t CastleSiegeMap = 30;
inline constexpr std::int32_t LandOfTrialsMap = 31;
inline constexpr std::int32_t SecondDevilSquareMap = 32;
inline constexpr std::int32_t AidaMap = 33;
inline constexpr std::int32_t CrywolfMap = 34;
inline constexpr std::int32_t SecondCrywolfMap = 35;
inline constexpr std::int32_t SeventhKalimaMap = 36;
inline constexpr std::int32_t CharacterSelectMap = 54;
inline constexpr std::int32_t LoginMap = 55;

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
