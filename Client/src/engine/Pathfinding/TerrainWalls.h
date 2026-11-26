#pragma once

#include "Data/Terrain/TerrainGrid.h"
#include "EngineApi.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

// 0x7d0d348's live map: the .att WORDs and the characters' bit (0x508f00; 06 § Caminar).
namespace Client::Pathfinding
{
inline constexpr std::uint16_t CharacterWallBit = 0x02;
inline constexpr std::uint16_t SafeZoneWallBit = 0x01;
// The wall bit 0x42a110 opens.
inline constexpr std::uint16_t BlockedWallBit = 0x04;
// 0x508f60: a character with +0x88 at 8 does not occupy its cell.
inline constexpr std::uint8_t NonBlockingKind = 8;

// Logical coordinates already decoded (+0x41c/+0x420), not the drawn position. The conditions are
// 0x508f49..0x508f67's; the hero is included too.
struct TerrainOccupant
{
    int logicalX = 0;
    int logicalY = 0;
    bool live = false;
    bool dying = false;
    std::uint8_t kind = 0;
};

// 0x508f14..0x508f34: 0x02 off the 65,536 WORDs, at the start of each characters' step.
ENGINE_API void ClearCharacterWalls(Data::Terrain::TerrainLayer<std::uint16_t>& cells);
// 0x509165: the 0x02 in a character's logical cell, through 0x5f82a0, which keeps the low eight bits of each
// coordinate.
ENGINE_API void MarkCharacterWall(Data::Terrain::TerrainLayer<std::uint16_t>& cells, int logicalX, int logicalY);

// One of 0x6c72bc's six cells, two bytes each (x, y).
struct GateCell
{
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};

// 0x6c72bc: the six cells 0x42a110 walks. *Inferred*: the castle gates of map 30 (the only one that sets 0x7239d9 to
// 1, 0x42db10).
inline constexpr std::array<GateCell, 6> GateCells{
    {{67, 114}, {93, 114}, {119, 114}, {81, 161}, {107, 161}, {93, 204}}};

// 0x42a110(0, 0, 0, 1), which 0x42da40 calls at the end of 0x510d00 with 0x7239d9 at 0 (*static*): for each cell
// (x, y) of GateCells, if the one below (x, y + 1) has 0x04 in its low byte, 0x5f8750(x − 1, y + 1, 4, 1, 4, 0)
// clears it (0x5f8730, and with ~0x04) from the four from x − 1 to x + 2 in that row. Runs on every map (Q-C276).
ENGINE_API void OpenTheGates(Data::Terrain::TerrainLayer<std::uint16_t>& cells);

// 0x558800(x, y, tx, ty): whether cell (tx, ty) is seen from (x, y), with a Bresenham over 0x7d0d348's WORDs from
// 0x5f82a0(x, y)'s index (each one's low eight bits). The long axis is the larger difference (Y if equal); each step
// adds the short one to an error that, above half the long one (signed division toward 0), also steps along the
// short one and subtracts the long one. It checks cells 0 to the long one, both ends included: one whose WORD is 4
// or more with no 0x20, 0x40 or 0x80 in its low byte blocks. Steps are linear (±1 and ±0x100), so an end off the grid
// goes on into the next row or leaves the array: nullopt where the original would read outside the 65,536 WORDs.
[[nodiscard]] ENGINE_API std::optional<bool> CanSeeTheCell(const Data::Terrain::TerrainLayer<std::uint16_t>& cells,
                                                           int x, int y, int targetX, int targetY);

struct ENGINE_API TerrainWalls
{
    Data::Terrain::TerrainLayer<std::uint16_t> cells{};

    // The owner of the protected variables opens and closes the coordinates and consumes their rand(). This pure part
    // clears 0x02 from the whole grid and marks the occupants.
    void Rebuild(std::span<const TerrainOccupant> occupants);
};
} // namespace Client::Pathfinding
