#pragma once

namespace Client::Data::Terrain
{
struct TerrainAttributes;
struct TerrainHeights;
} // namespace Client::Data::Terrain

// The terrain's height at a point of the world: 0x5f93c0, which the login's embers ask for as they fall. Checked
// against the emulated real code (tests/vectors/world55/leaves.txt).
namespace Client::Terrain
{
// The initial height of 0x6c9d5c; the shared camera leaves it at 1,201 with the first 0x40.
inline constexpr float FixedTerrainHeight = 1200.0f;

// 0x5f93c0: with x or y negative, 0. The cell: the column from x · 0.01 stored as float; the row from y · 0.01 in 53
// bits (what ftol truncates on the stack); the index, row · 256 + column in 32 bits. With the index outside the grid or
// the 0x40 of its attribute, `fixedHeight`; otherwise between the heights of the four corners (the row and the next,
// the column and the next, wrapping from 255 to 0), first by the fraction of y (the product stored as float minus the
// row, also in float: it can give 1) in each column and then by that of x (on the stack), all in 53 bits. Returns what
// stays on the x87 stack. Without the scene flag of 0x6d5dc8 (in the login, scene 2, it does not check it).
[[nodiscard]] double TerrainHeightAt(const Data::Terrain::TerrainHeights& heights,
                                     const Data::Terrain::TerrainAttributes& attributes, float x, float y,
                                     float fixedHeight = FixedTerrainHeight);
} // namespace Client::Terrain
