#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// The three objects of 0x412140: a 0x20-byte base, a 7 × 5 table in class 1 and a vector plus a table in class 2.
namespace Client::CastleSiege
{
inline constexpr std::size_t SiegeStatusSlots = 7;
struct SiegePanelState
{
    std::int32_t offsetX = 0; // +4 and +8, the map window the draw leaves
    std::int32_t offsetY = 0;
    std::int32_t x = 489;     // +0xc
    std::int32_t y = 259;     // +0x10
    std::int32_t page = 1;    // +0x14, scale 1 or 2
    std::uint8_t visible = 1; // +0x19
    float alpha = 1.0f;       // +0x1c
    // +0x20 (class 1), +0x34 (class 2). Order: number, x, y, state, duration.
    std::array<std::array<std::uint8_t, 5>, SiegeStatusSlots> status{};
    // Class 2: +0x20 initially 1; 0x411ed0 sets it to 0 after each append.
    std::uint8_t membersHidden = 1;
    std::vector<std::array<std::uint8_t, 3>> members{};
    // +0x57..+0x5b of class 2: selected row, coordinates, order and duration.
    std::uint8_t selected = 0;
    std::uint8_t targetX = 0;
    std::uint8_t targetY = 0;
    std::uint8_t command = 3;
    std::uint8_t targetTtl = 0;
};
} // namespace Client::CastleSiege
