#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Client::Ui
{
class PanelRegistry;
}

namespace Client::Kernel
{
struct ItemState;
}

namespace Client::Kernel
{
enum class CursorGrid : std::uint8_t;
} // namespace Client::Kernel

// The right click on an inventory grid item from 0x5c7bc6 (CL9 milestone 5, phase 2; 06 § Los ítems del piso, "Usar
// desde la grilla"): what 0x5c4110 does after the potions, the portal scroll, the luck box and the event tickets.
namespace Client::Items
{
struct InventoryMoveContext;

// The grid of 0x5c4110's right-clicked item ([EBP + 0x10]): the inventory's (0x78a9bb0) or the own trade grid
// (0x78a92a8, CL9 milestone 7, 08 P-C235), both of 8 columns. With the trade grid the branch is the same: the item
// comes from the cell (EBX) and its top left cell with this grid's width, without the 12.
struct UsedGrid
{
    std::span<Kernel::ItemState> cells;
    Kernel::CursorGrid grid;
};

// 0x5c7bc6..0x5c9b0b with the item of `grid`'s cell `index` (the one under the mouse; the top left one comes from its
// +0x42 and +0x43):
// - the skill books and orbs (0x1e00..0x1fff, 0x1807..0x180e and 0x1810..0x1818; 0x5c8fb9): with the own record open
//   (its 3848 rand() when closing it), the level against Item.bmd's +0x3a and the energy and strength with what the
//   items add against the item's +0x22 and +0x1e; with all three, what UseInventoryItem does; otherwise, nothing;
// - the fruit 0x1a0f (0x5c7c10), if the top left cell of the inventory (0x78a9bb0, even if the item is from the trade
//   grid: 07 Q-C964) also has it: the level from the own record (opening and closing it); under 10, notice 749;
//   otherwise, the item of that inventory cell in 0x78a59c8, and with the record open again the equipment and the
//   class: with something worn, notice 1909; the level 4 fruit (the dword +4 at 0x20) without class 4, 1905;
//   otherwise, dialog 0x96 with text 376 and the item copied in 0x78a59c8;
// - the 0x1a0b (0x5c8448): of level 0, or of level 1 without the hero as guild master (+0x368 at 0x80), what
//   UseInventoryItem does; otherwise, nothing;
// - the others (0x5c8e37): the copy in the cursor (0x7879f80) with its grid in 0x78ab9b8 and, for the inventory's,
//   0x78abb28 at 1; with the warehouse open (IsOpen(0x40), no rand()), 0x5c9e10 looks for an empty footprint from
//   right to left and from bottom to top. With room, it takes out the source, keeps the cursor and leaves the
//   temporary target for 0x5ca250; without room, or with 0x7d368a1 at 0, the cursor goes back to zeros.
// With the trade grid, everything that reaches UseInventoryItem gives notice 474: the trade is open.
[[nodiscard]] Core::Expected<void, std::string>
UseTheLaterGridItem(const InventoryMoveContext& context, Ui::PanelRegistry& panels, UsedGrid grid, std::size_t index);

// Before the potions (0x5c67e9, 0x5c6c36 and 0x5c7010): 0x1a1d, 0x1c13 and 0x1a12.
[[nodiscard]] bool IsAnEventTicket(std::int32_t type);

// The 0x1a1d checks the NPC shop, the warehouse, the trade, 0x1000 and 0x4000 (0x5c67f3..0x5c685f), in that order and
// up to the first open one, with the shop's and the trade's rand(); with one open it does nothing (0x5c9b0b).
// Otherwise, and with the other two, it sends C1 04 91 with kind 4/1/2; only 0x1a1d keeps the cell + 12 and the
// item's copy.
[[nodiscard]] Core::Expected<void, std::string>
UseTheEventTicket(const InventoryMoveContext& context, Ui::PanelRegistry& panels, UsedGrid grid, std::size_t index);
} // namespace Client::Items
