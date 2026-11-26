#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// An inventory independent of Data and of the ASM flow, not results obtained by running MuRet.
// Provenance and limits: docs/superpowers/plans/2026-11-09-cl7/map-sweep-2026-11-10.md.
namespace Client::Tests
{
enum class MapSweepCut : std::uint8_t
{
    None,
    MissingHeight,
    MissingWorld,
    LoginSpecials,
};

struct MapSweepWorldCase
{
    std::uint8_t folder;
    std::size_t records;
    std::int16_t firstType;
    std::int16_t lastType;
    std::uint32_t layer1Crc;
    std::uint32_t layer2Crc;
    std::uint32_t heightByteSum;
};

// .obj header and ends; CRC-32 of each byte layer of the decrypted .map; sum of the 65,536 height bytes from 0x43c.
// World36 does not exist.
inline constexpr std::array<MapSweepWorldCase, 21> MapSweepWorlds{{
    {1, 2870, 30, 60, 0x813cf6d6, 0x587696b1, 6969485},
    {2, 4488, 0, 46, 0x55a8df0b, 0x5c5a1dc9, 7533267},
    {3, 2144, 18, 0, 0x975812a4, 0x2541fa2f, 7193603},
    {4, 9399, 23, 32, 0x8722f2f5, 0x2a92f082, 8156075},
    {5, 5380, 1, 22, 0x962f03b5, 0xe33b8fbd, 5057702},
    {6, 0, 0, 0, 0xd7978eeb, 0xdeab7e4e, 0},
    {7, 1489, 20, 26, 0x6998c49f, 0x335230f3, 9797125},
    {8, 5205, 22, 25, 0x69140a68, 0x30443940, 10394040},
    {9, 3327, 40, 56, 0xd6b8cff3, 0xf9ede126, 7929859},
    {10, 564, 0, 4, 0xd7978eeb, 0xdeab7e4e, 9888340},
    {11, 670, 5, 5, 0xc829b541, 0xdc8e5b83, 12451840},
    {12, 210, 33, 25, 0xed10e13b, 0x7f95b44b, 7227563},
    {19, 194, 16, 12, 0x0de42c85, 0xb5d5495d, 0},
    {25, 2266, 37, 41, 0xfd0264b0, 0x05c362d3, 10485444},
    {31, 3724, 2, 5, 0x37248352, 0x87bf4e12, 8869608},
    {32, 5638, 29, 14, 0x3cd7b691, 0xc1b7444d, 9915517},
    {34, 2979, 14, 6, 0xfa0814cf, 0xf3980671, 8503067},
    {35, 6644, 33, 78, 0xe78056ec, 0xed180276, 6761191},
    {36, 0, 0, 0, 0, 0, 0},
    {55, 192, 0, 16, 0x0d370054, 0x1a977860, 8499475},
    {56, 276, 27, 3, 0x00f48c00, 0x92fb0d8c, 7941298},
}};

inline constexpr std::uint32_t MapSweepSeed = 0x5eed;
// The original's CRT: seed*0x343fd+0x269ec3, modulo 2^32, without running the port's CrtRandom.
inline constexpr std::uint32_t MapSweepAfterGrass = 0xad6d5eed;  // 65,536 draws.
inline constexpr std::uint32_t MapSweepAfterNoria = 0xb58b8a29;  // 12 for the tower + 65,536 for the grass.
inline constexpr std::uint32_t MapSweepAfterTrials = 0xe254a308; // 65,536 + 205 of types 27/54 in World32.
// 65,536 + 24: the rand() % 1000 of each 0x29 of World31 at creation (0x42c711).
inline constexpr std::uint32_t MapSweepAfterCastle = 0x141dd1a5;

struct MapSweepCase
{
    std::uint8_t nominal;
    std::uint8_t effective;
    std::uint8_t folder;
    MapSweepCut cut;
    std::size_t created;
    std::size_t registrations;
    std::uint32_t randomState;
    // Gate.bmd counts for the NOMINAL map: flag 0 (/move), 1 (entrance), 2 (arrival).
    std::array<std::size_t, 3> gates;
    // 0x447d70 builds the event object (9, 11 to 23 and 32, which the load turns into 9).
    bool eventObject;
};

// Aliases keep their effective map except 0x61a560's explicit 32 -> 9 guard.
// Every record of these .obj passes the native sector, including Devias' NaNs (Q-C186).
// The specials come before the file: two in Devias and a tower in Noria.
// Noria creates five effects: four 322/323 take three rand each; 324 takes none.
// World4 holds 16 records of type 8 and 53 of 38: 69 auxiliary entries, counted apart from K16.
// World31: five lancers before the 3,724 records; the 0x4d (one) and the 0x54 (37) are registered with 0x606630.
inline constexpr std::array<MapSweepCase, 39> MapSweepCases{{
    {0, 0, 1, MapSweepCut::None, 2870, 110, MapSweepAfterGrass, {1, 4, 4}, false},
    {1, 1, 2, MapSweepCut::None, 4488, 39, MapSweepAfterGrass, {0, 7, 7}, false},
    {2, 2, 3, MapSweepCut::None, 2146, 154, MapSweepAfterGrass, {4, 2, 2}, false},
    {3, 3, 4, MapSweepCut::None, 9400, 69, MapSweepAfterNoria, {1, 3, 3}, false},
    {4, 4, 5, MapSweepCut::None, 5380, 0, MapSweepAfterGrass, {1, 8, 8}, false},
    {5, 5, 6, MapSweepCut::MissingHeight, 0, 0, MapSweepAfterGrass, {0, 0, 0}, false},
    {6, 6, 7, MapSweepCut::None, 1489, 0, MapSweepAfterGrass, {3, 1, 0}, false},
    {7, 7, 8, MapSweepCut::None, 5205, 7, MapSweepAfterGrass, {3, 2, 2}, false},
    {8, 8, 9, MapSweepCut::None, 3327, 13, MapSweepAfterGrass, {2, 1, 1}, false},
    {9, 9, 10, MapSweepCut::None, 564, 0, MapSweepAfterGrass, {4, 0, 0}, true},
    {10, 10, 11, MapSweepCut::None, 670, 0, MapSweepAfterGrass, {0, 1, 1}, false},
    {11, 11, 12, MapSweepCut::None, 210, 0, MapSweepAfterGrass, {1, 0, 0}, true},
    {12, 12, 12, MapSweepCut::None, 210, 0, MapSweepAfterGrass, {1, 0, 0}, true},
    {13, 13, 12, MapSweepCut::None, 210, 0, MapSweepAfterGrass, {1, 0, 0}, true},
    {14, 14, 12, MapSweepCut::None, 210, 0, MapSweepAfterGrass, {1, 0, 0}, true},
    {15, 15, 12, MapSweepCut::None, 210, 0, MapSweepAfterGrass, {1, 0, 0}, true},
    {16, 16, 12, MapSweepCut::None, 210, 0, MapSweepAfterGrass, {1, 0, 0}, true},
    {17, 17, 12, MapSweepCut::None, 210, 0, MapSweepAfterGrass, {1, 0, 0}, true},
    {18, 18, 19, MapSweepCut::None, 194, 0, MapSweepAfterGrass, {1, 0, 0}, true},
    {19, 19, 19, MapSweepCut::None, 194, 0, MapSweepAfterGrass, {1, 0, 0}, true},
    {20, 20, 19, MapSweepCut::None, 194, 0, MapSweepAfterGrass, {1, 0, 0}, true},
    {21, 21, 19, MapSweepCut::None, 194, 0, MapSweepAfterGrass, {1, 0, 0}, true},
    {22, 22, 19, MapSweepCut::None, 194, 0, MapSweepAfterGrass, {1, 0, 0}, true},
    {23, 23, 19, MapSweepCut::None, 194, 0, MapSweepAfterGrass, {1, 0, 0}, true},
    {24, 24, 25, MapSweepCut::None, 2266, 0, MapSweepAfterGrass, {1, 0, 0}, false},
    {25, 25, 25, MapSweepCut::None, 2266, 0, MapSweepAfterGrass, {1, 0, 0}, false},
    {26, 26, 25, MapSweepCut::None, 2266, 0, MapSweepAfterGrass, {1, 0, 0}, false},
    {27, 27, 25, MapSweepCut::None, 2266, 0, MapSweepAfterGrass, {1, 0, 0}, false},
    {28, 28, 25, MapSweepCut::None, 2266, 0, MapSweepAfterGrass, {1, 0, 0}, false},
    {29, 29, 25, MapSweepCut::None, 2266, 0, MapSweepAfterGrass, {1, 0, 0}, false},
    {30, 30, 31, MapSweepCut::None, 3729, 38, MapSweepAfterCastle, {6, 5, 5}, false},
    {31, 31, 32, MapSweepCut::None, 5638, 0, MapSweepAfterTrials, {0, 1, 1}, false},
    {32, 9, 10, MapSweepCut::None, 564, 0, MapSweepAfterGrass, {2, 0, 0}, true},
    {33, 33, 34, MapSweepCut::None, 2979, 0, MapSweepAfterGrass, {1, 1, 2}, false},
    {34, 34, 35, MapSweepCut::None, 6644, 0, MapSweepAfterGrass, {1, 2, 2}, false},
    {35, 35, 36, MapSweepCut::MissingWorld, 0, 0, MapSweepAfterGrass, {0, 0, 0}, false},
    {36, 36, 25, MapSweepCut::None, 2266, 0, MapSweepAfterGrass, {1, 0, 0}, false},
    {54, 54, 55, MapSweepCut::None, 192, 0, MapSweepAfterGrass, {0, 0, 0}, false},
    {55, 55, 56, MapSweepCut::LoginSpecials, 0, 0, MapSweepSeed, {0, 0, 0}, false},
}};
} // namespace Client::Tests
