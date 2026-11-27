#pragma once

#include "Kernel/ItemState.h"

#include <array>
#include <cstddef>

// The own inventory's two grids, which the F3 10 fills: arrays of 0x48-byte ItemStates in the .bss, which WinMain does
// not touch and which last from one scene to the next. Contract: Client/docs/research/06-render-escenas-y-ui.md, "El F3
// 10, el inventario". What each shows in the interface is unread (CL9): the second, the one of places from 76 on, could
// be the personal shop (*inferred* from its size).
namespace Client::Kernel
{
// 0x5bc740 starts a row every 8 cells: the 8 that 0x4c55a0 passes it (0x4c5e83) for the row, and the fixed `% 8` and
// `/ 8` of 0x5bcc68..0x5bcc81 for the column and row of +0x42 and +0x43.
inline constexpr std::size_t InventoryColumns = 8;
// [0x78a9bb0], the one of places 12 to 75: the one 0x4c55a0 clears up to 0x78aadec − 0x3c (0x4c56be).
inline constexpr std::size_t InventoryCellCount = 64;
// [0x78ab0b8], the one of places from 76 on: up to 0x78ab9f4 − 0x3c (0x4c56db).
inline constexpr std::size_t SecondGridCellCount = 32;

struct OwnInventory
{
    // The F3 10's common initialization (0x4c56a8..0x4c56e0), after the own record is closed: in the 96 cells, the type
    // at −1 and +0x3c (occupied) and +0x1b (flags1b) at 0, as in the equipment.
    void ClearForInventoryReply();

    std::array<Kernel::ItemState, InventoryCellCount> cells{};
    std::array<Kernel::ItemState, SecondGridCellCount> secondCells{};
};
} // namespace Client::Kernel
