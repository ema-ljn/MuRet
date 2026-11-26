#pragma once

#include <array>
#include <cstdint>
#include <string>

// Each map's World<N> folder, as 0x61a560 builds it when loading a world (05 § La carpeta de cada mapa, *static*).
// Its files are asked with that N too: "Data\%s\EncTerrain%d.map" (0x6d12e8), .att and .obj. Nothing in MuRet builds
// "World1" by hand.
namespace Client::Maps
{
// The maps that reach 0x61a560: 0 to 36, the game's (the handlers that send a map pass the packet's byte without a
// range check, Q-C58), 54 for the selection (0x633f44, in 0x633f20) and 55 for the login (0x6349b2, in 0x6349a0).
// The all-maps tests go through this list.
inline constexpr std::array<std::uint8_t, 39> OriginalMaps = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
                                                              13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
                                                              26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 54, 55};

// 0x61a560's local projection for a byte: argument and observed map match and nothing changes between the two phases
// (05 § La carpeta de cada mapa). Composes MapAtWorldLoadStart and WorldFolderAfterModels: 32 → World10 (Q-C58: the
// guard changes the global map 0x6c9dd0); event aliases share a folder, not an identity. Returns more than a byte
// because 255 gives 256.
[[nodiscard]] std::uint16_t WorldFolderNumber(std::uint8_t map);

// Like 0x6c7330's "World%d": "World56" for 55.
[[nodiscard]] std::string WorldFolder(std::uint8_t map);
} // namespace Client::Maps
