#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace Client::Data::Terrain
{
struct TerrainAttributes;
}

// The marks the events set and clear in the loaded map's .att (0x7d0d348, one WORD per cell): packets 46 (04 § Los
// eventos) and the Blood Castle and Chaos Castle collapses (06 § Los dibujos de los eventos).
namespace Client::Terrain
{
// A rectangle of cells of 0x5f8750: from (x, y), `width` columns and `height` rows.
struct TerrainArea
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
};

// 0x5f8710 (OR, with `add`) and 0x5f8730 (AND with the complement) on WORD (y << 8) + x. Without the .att, or with the
// cell outside the 0x10000 WORDs, fails at `where` without touching anything.
[[nodiscard]] Core::Expected<void, std::string> MarkTheTerrainCell(Data::Terrain::TerrainAttributes* attributes,
                                                                   std::int32_t x, std::int32_t y,
                                                                   std::uint8_t attribute, bool add,
                                                                   std::string_view where);

// 0x5f8750(x, y, width, height, attribute, add): row by row, from y to y + height − 1, and in each from x to
// x + width − 1; with height or width at 0 or less, nothing. Stops at the first cell it cannot mark, with the earlier
// ones done.
[[nodiscard]] Core::Expected<void, std::string> MarkTheTerrainArea(Data::Terrain::TerrainAttributes* attributes,
                                                                   const TerrainArea& area, std::uint8_t attribute,
                                                                   bool add, std::string_view where);
} // namespace Client::Terrain
