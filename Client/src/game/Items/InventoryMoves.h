#pragma once

#include "Core/Expected.h"
#include "Tables/ItemTable.h"
#include "Pathfinding/PathCell.h"
#include "Protocol/Items/FloorItems.h"
#include "Protocol/Items/Inventory.h"
#include "Protocol/Outgoing.h"
#include "Npc/NpcWindowState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// Picking up, dropping and equipping the cursor item (CL8 milestone 4; 06 § El inventario, "El ítem del cursor"): the
// non-UI part of 0x5bd750, 0x5be150, 0x5ca250's inventory stretch, 0x5bec50, 0x5c4110 and MoveHero, and the replies
// 24, 26, 27, 28, 2A, F3 13 and F3 14. The grid is the inventory's (0x78a9bb0, 8 × 8) with the equipment in slots 0
// to 11. The shop and warehouse grid and the chaos box are CL9 milestone 6's; the others (trade, personal shop and
// the trainer's) stop with their address.
namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Tables
{
class TextTable;
}

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
class WorldState;
enum class CursorGrid : std::uint8_t;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

namespace Client::Items
{

// What the UI steps and MoveHero ask from outside to move items. Empty or nullptr: what needs it stops.
struct InventoryServices
{
    // *[0x7758550], Item.bmd: the width (+0x24), the height (+0x25), the equipment slot (+0x22) and +0x49 of each type.
    const Tables::ItemTable* items = nullptr;
    // 0x512a30 on the published hero (DressTransitionHero), with the sound 0x4f2250 asks for in lastSoundId.
    std::function<Core::Expected<void, std::string>()> dressTheHero{};
    // 0x420cf0: releases the raven and, outside maps 18 to 23, creates it again (ReplaceTheRaven).
    std::function<Core::Expected<void, std::string>()> replaceTheRaven{};
    // 0x555190(name, Text.bmd's text `index`, type): a chat message, with type 2 (Ui::ErrorMessage) or 1
    // (Ui::SystemMessage, the arrows being equipped). The names the original passes (0x77f3bdc to 0x77f3be4, 0x78abb44,
    // 0x78abb60 to 0x78abb6c) are buffers nobody writes: they go empty (*inferred*, 08 P-C72).
    std::function<Core::Expected<void, std::string>(std::size_t index, std::int32_t type)> systemMessage{};
    // The C1 0E 24 in C3 through the session (ServerSession::SendItemMove), after the send's two rand().
    std::function<Core::Expected<void, std::string>(const Protocol::Items::ItemMove&)> sendItemMove{};
    // CL9 milestone 5. For the texts built with sprintf.
    const Tables::TextTable* texts = nullptr;
    // 0x555190 with an already built text (sprintf with a Text.bmd text as the format) and the empty name.
    std::function<Core::Expected<void, std::string>(std::string_view text, std::int32_t type)> chatLine{};
    // The C1 06 23, the C1 05 22 and the C1 06 26 in C3 through the session, after the send's two rand().
    std::function<Core::Expected<void, std::string>(const Protocol::Items::Drop&)> sendDrop{};
    std::function<Core::Expected<void, std::string>(std::uint16_t index)> sendPickUp{};
    std::function<Core::Expected<void, std::string>(const Protocol::Items::UseItem&)> sendUseItem{};
    // 0x5fc260(1) and 0x5fac90 with the mouse ray, like MoveHero (0x59de04..0x59de78): the terrain cell under the mouse
    // (0x7d366e0 and 0x7d366e4 · 0.01) or nothing.
    std::function<Core::Expected<std::optional<Pathfinding::PathCell>, std::string>()> pickTheGround{};
    // CL9 milestone 7. Another game packet through the session, with no rand() (the C3's two are drawn by whoever
    // builds it): the trade's C1 04 3C 00 sent to drop an inventory item on its grid (0x5cf438).
    std::function<void(Protocol::Outgoing)> sendGamePacket{};
};

// The services of `resources`, without the send: Item.bmd, 0x512a30 with Player.bmd, the raven and the messages.
[[nodiscard]] InventoryServices InventoryServicesOf(const Scenes::WorldEntryResources& resources);

// The width (+0x24) and the height (+0x25) of `type`'s Item.bmd record.
struct ItemFootprint
{
    std::int32_t width = 0;
    std::int32_t height = 0;
};

[[nodiscard]] Core::Expected<ItemFootprint, std::string> ItemFootprintOf(const Tables::ItemTable* items,
                                                                         std::uint16_t type, std::string_view where);

// The cell under the mouse for the cursor item on a grid from (left, top): _ftol of (mouse − corner) · 0.05f − size
// · 0.5f + 0.5f, on the x87 (0x5ca2af..0x5ca2fd), which truncates toward 0.
struct GridCell
{
    std::int32_t column = 0;
    std::int32_t row = 0;
};

[[nodiscard]] GridCell CellUnderTheMouse(std::int32_t mouseX, std::int32_t mouseY, std::int32_t left, std::int32_t top,
                                         ItemFootprint footprint);

// 0x5c9e10: columns from right to left, rows from bottom to top, without stacking.
[[nodiscard]] std::optional<GridCell> FindFreeGridCell(std::span<const Kernel::ItemState> cells, std::int32_t columns,
                                                       std::int32_t rows, ItemFootprint footprint);

// 0x5c9b40 with the inventory grid: whether the cursor item fits in cell `cell`. It counts the empty cells (type
// 0xffff) of its footprint inside the grid; with all of them, yes. Otherwise, with the same type in the top left cell
// (row · columns + column, without checking whether the column overflows), the stackable ones: 0x1c00..0x1c08, 0x807
// and 0x80f with the same +4, 0x1c1d and 0x1a20..0x1a22. Outside the array the original reads anyway (Q-C474): MuRet
// does not stack (*inferred*, 08 P-C72).
[[nodiscard]] bool ItemFitsInTheGrid(const Kernel::ItemState& cursor, std::span<const Kernel::ItemState> cells,
                                     std::int32_t columns, std::int32_t rows, GridCell cell, ItemFootprint footprint);

struct InventoryMoveContext
{
    Kernel::WorldState& world;
    Crt::CrtRandom& random;
    const InventoryServices& services;
};

// 0x5bd750(slot, 0x78a9bb0, 8): removes `slot`'s item. 0xff, nothing. From 12 on, empties the footprint of that
// cell's item from its column and its row (type 0xffff and +0x3c at 0), in the inventory grid up to 75 and in the
// second one from 76. From 0 to 11, with the own record open: empty, nothing; otherwise, the raven (0x421d60) if it is
// slot 1's 0x1a05, the type at 0xffff, +0x3c at −1 and +0x1b and +0x1c at 0; then, in slots 0 and 1, the arrows
// (0x561840) and the hero's hand, and in 8 the helpers (0x443d40) and the hero's pet.
[[nodiscard]] Core::Expected<void, std::string> RemoveInventoryItem(const InventoryMoveContext& context,
                                                                    std::int32_t slot);

// 0x5be150: with an item in the cursor, puts it back in its slot ([0x78a6758]): the footprint in the grid, with +0x3c
// at 1 in the cell at its column and its row, or the equipment (with the pet, the raven and the ring first); then the
// count at 0, 0x512a30 and sound 0x1d.
[[nodiscard]] Core::Expected<void, std::string> ReturnTheCursorItem(const InventoryMoveContext& context);

// 0x5bc740(grid, columns, rows, slot, data, 0) with another grid (0x5bd307.. and the same tail): the `24` with 2 (the
// warehouse, 0x78a7070) and with 3 (the chaos box, 0x7d2d380).
[[nodiscard]] Core::Expected<void, std::string> PlaceMovedItemInOtherGrid(std::span<Kernel::ItemState> cells,
                                                                          std::int32_t columns, std::uint8_t slot,
                                                                          const Protocol::Items::ItemData& item,
                                                                          const Scenes::WorldEntryResources& resources,
                                                                          std::string_view where);

// The client 24's pkt[3] and pkt[0xc]: the source and target grids, 0 the inventory and 1 the own trade grid
// (0x78a92a8; 0x5ce5af, 0x5d02a0 and 0x5d0898).
inline constexpr std::uint8_t InventoryGridFlag = 0;
inline constexpr std::uint8_t TradeGridFlag = 1;
// 0x78ab0b8 (0x5cc818.., 0x5d5608.. and 0x5dcd4b.., CL9 milestone 7).
inline constexpr std::uint8_t ShopGridFlag = 4;

// The cursor item's C1 0E 24: the lock 0x78abb39 at 1, the send's two rand() in C3 and the packet, from grid
// `sourceFlag` and slot [0x78a6758] to grid `targetFlag` and `targetSlot`, with bytes 0, +4, +0x1a, +0x1b, +0x1c,
// the type >> 5 & 0xf0 and 0.
[[nodiscard]] Core::Expected<void, std::string> SendTheCursorItem(const InventoryMoveContext& context,
                                                                  std::uint8_t targetSlot,
                                                                  std::uint8_t sourceFlag = InventoryGridFlag,
                                                                  std::uint8_t targetFlag = InventoryGridFlag);

// 0x449d80(source, slot, target, slot): with the lock 0x78abb39 at 0, the lock at 1, the send's two rand() in C3 and
// the C1 0E 24 with the source grid and slot, the cursor item's seven bytes and the target grid and slot. With the
// lock, nothing. The grids: 0 the inventory, 2 the warehouse, 3 the chaos box and 5 the trainer's.
[[nodiscard]] Core::Expected<void, std::string>
SendTheCursorItemBetween(const InventoryMoveContext& context, std::uint8_t sourceGrid, std::uint8_t sourceSlot,
                         std::uint8_t targetGrid, std::uint8_t targetSlot);

// The C1 0E 24's grid numbers (0x5d1f8b, 0x5d3c68, 0x5cb4ed and 0x449d6a).
inline constexpr std::uint8_t InventoryMoveGrid = 0;
inline constexpr std::uint8_t WarehouseMoveGrid = 2;
inline constexpr std::uint8_t ChaosBoxMoveGrid = 3;
inline constexpr std::uint8_t TrainerBoxMoveGrid = 5;

// 0x5cb3af..0x5cb455 and 0x5ea1ad..0x5ea227: the warehouse keypad (0x7879fc8, ten words) with 0 to 9 and twenty swaps
// of two random keys (rand() % 10 twice, no swap if they are the same).
void ShuffleTheKeypad(Crt::CrtRandom& random, std::array<std::uint16_t, Npc::WarehouseKeyCount>& keys);

// 0x5c52b1..0x5c5320, from 0x5c4110: picks up the item of the grid's cell `cell` (the one at its column and its row):
// the click consumed, the copy to the cursor from the top left one, the source grid, the slot, 0x5bd750 and the item
// under the mouse cleared. What follows (the trade's 0x6599a0 and the sound) belongs to the UI.
[[nodiscard]] Core::Expected<void, std::string> PickUpFromTheGrid(const InventoryMoveContext& context,
                                                                  std::size_t cell);

// 0x5c52b1..0x5c5358 with another 8-column grid (the shop and warehouse one, 0x78a7070, the chaos box, 0x7d2d380, or
// the trainer's, 0x723c80): the same with the slot without the 12 (row · 8 + column) and 0x5bd750(slot, grid, 8),
// which empties the footprint from that cell's column and row.
[[nodiscard]] Core::Expected<void, std::string> PickUpFromOtherGrid(const InventoryMoveContext& context,
                                                                    Kernel::CursorGrid grid, std::size_t cell);

// The same from the own trade grid (0x78a92a8, 8 × 4; CL9 milestone 7, 08 P-C205 a): the slot is the top left cell,
// without the 12 (0x5c531b..0x5c5345), and 0x5bd750 empties its footprint in that grid (0x5be08c..).
[[nodiscard]] Core::Expected<void, std::string> PickUpFromTheTradeGrid(const InventoryMoveContext& context,
                                                                       std::size_t cell);

// 0x5c5112..0x5c5224, from 0x5c4110 with a personal shop open and the click (CL9 milestone 7, 08 P-C208 b): the click
// consumed and, with the top left item of slot `slot`'s grid (from 0x4c the shop's, from 12 the inventory's), the
// copy to the cursor, the source grid `grid`, the slot and 0x5bd750. Unlike picking up (0x5c52b1), it does not clear
// the copy of the item under the mouse (0x787a518) nor 0x78abb28 and asks for no sound.
[[nodiscard]] Core::Expected<void, std::string> PickUpForTheShop(const InventoryMoveContext& context, std::int32_t slot,
                                                                 Kernel::CursorGrid grid);

// 0x5c2c65..0x5c2dd0, from 0x5bec50 with no item in the cursor and with the click: with 0x576794c, nothing; with
// 0x78abb0c, repair (stops); otherwise it consumes the click and, except what map 10 or Chaos Castle do not let be
// taken off, moves the slot's item to the cursor, dresses the hero, clears the item under the mouse, asks for sound
// 0x1d and turns off the helpers (slot 8) or the raven (slot 1's 0x1a05). The caller has the own record open.
[[nodiscard]] Core::Expected<void, std::string> PickUpFromTheEquipment(const InventoryMoveContext& context,
                                                                       std::size_t slot);

// 0x59bd3e..0x59cd23, from MoveHero (0x59bd01) with the cursor empty, the right hand empty and a weapon in the left:
// in one access to the own record, slot 1's item to the cursor (the 0x48 bytes, with its +0x3c) and the slot at
// 0xffff with +4 at 0; 0x512a30 on the published hero; the inventory grid, slot 1 and target 0; and, without the
// lock, the C1 0E 24 from 1 to 0.
[[nodiscard]] Core::Expected<void, std::string> MoveTheLeftWeaponToTheRight(const InventoryMoveContext& context);

// 0x561c50, from MoveHero's attack click (0x59d65f, CL9 milestone 3): whether the shooting weapon has something to
// shoot. It opens and closes the own record to read the hands; with a crossbow on the right (0x808..0x80e, 0x810 and
// 0x812..0x9ff) it checks the bolts (0x807) on the left with their durability (+0xbce), and otherwise, with a bow on
// the left (0x800..0x806, 0x811, 0x814 and 0x815), the arrows (0x80f) on the right with theirs (+0xb86): in another
// access, without them or spent, 0x561840 (which looks for them in the inventory and equips them) and false; with
// them, true. Without a shooting weapon, true.
[[nodiscard]] Core::Expected<bool, std::string> CheckTheArrows(const InventoryMoveContext& context);

// 0x5c35ad, from 0x5bec50 with no item in the cursor and without the click: the slot's mark (3 if a requirement is
// missing, with +0x26 against the strength, Q-C475; otherwise 2), the item under the mouse and its tooltip's corner.
void HoverTheEquipment(const InventoryMoveContext& context, std::size_t slot, std::int32_t left, std::int32_t top,
                       float width);

// 0x5bee04..0x5bf2a0, from 0x5bec50 with an item in the cursor and the mouse on the slot: whether it goes there (the
// hands, the published hero's class, the requirements, the map and Crywolf). If it goes, mark 2 and, with the click,
// the target (0x77f3cc8), Fenrir's notice 1862 and the 24; otherwise mark 3 and, with the click, 0x5be150. From
// another grid (0x5c007c), the 24 from the warehouse (or, with its lock, the password and the shuffled keypad), the
// chaos box or the trainer's (0x5c1d80), without the notice; the trade and the personal shop stop (0x5c1d90,
// milestone 7). The caller has the own record open.
[[nodiscard]] Core::Expected<void, std::string> DropOnTheEquipment(const InventoryMoveContext& context,
                                                                   std::size_t slot);

// 0x5ca250(x, y, 0x78a9bb0, 8, 8, 0), the inventory stretch: the cell under the mouse, whether it fits (0x5c9b40), its
// footprint's marks inside the grid and, with the click, the 24 to that cell's slot, the return to its slot or
// nothing. If it does not fit, with the inventory open, a jewel of bless, soul or life goes on that cell's item
// (0x5d82a8, CL9 milestone 5): with its type, its level and the queries, the C1 06 26 with the jewel's slot and the
// item's; Bless on the 0x1a25, dialog 0xa9. With the item lifted from the own trade grid (0x5ce4fe..0x5cf3a0, CL9
// milestone 7): with the lock released, the 24 from grid 1; with or without it, without the own OK, 0x78abac8 at
// 0x96. Returns what the original returns: true if the click stayed on the grid.
struct GridDrop
{
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    std::int32_t left = 0;
    std::int32_t top = 0;
    // IsOpen(0x4000000), which draws no rand().
    bool inventoryOpen = false;
    // 0x659290's queries for the jewel (0x5d838c..0x5d83c2). Without it, the jewel that reaches them stops.
    Ui::PanelRegistry* panels = nullptr;
};

[[nodiscard]] Core::Expected<bool, std::string> DropOnTheInventoryGrid(const InventoryMoveContext& context,
                                                                       const GridDrop& drop);

// 0x5ca250(x, y, grid, 8, rows, type) with the warehouse (0x78a7070, 15 rows, type 2, from 0x5deb57), the chaos box
// (0x7d2d380, 4 rows, type 3, from 0x5debe4) and the trainer's (0x723c80, 4 rows, type 5, from 0x44aca9): the cell
// under the mouse, whether it fits (0x5c9b40), its footprint's marks inside the grid and, with the click and if it
// fits, except the items that do not go to another grid (0x5cf3a5..0x5cf40b), 0x449d80's C1 0E 24 from the cursor's
// grid (0 the inventory; otherwise, the same one) to the slot row · 8 + column. With 0x724580 at 3, only its mix's
// jewels and 0x1a1f go to the trainer box (0x44acd0). The chaos box with the mix not at 0 and the Castle Siege list
// closed does not call 0x5c9b40 and does not allow it (0x5ca333, Q-C921).
struct OtherGridDrop
{
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    std::int32_t left = 0;
    std::int32_t top = 0;
    // CursorGrid::NpcGrid, CursorGrid::ChaosBox or CursorGrid::TrainerBox.
    Kernel::CursorGrid grid{};
};

[[nodiscard]] Core::Expected<bool, std::string> DropOnOtherGrid(const InventoryMoveContext& context,
                                                                const OtherGridDrop& drop);

// 0x5ca250(x + 15, y + 0x10e, 0x78a92a8, 8, 4, 1), from 0x5deb1c with the trade open (CL9 milestone 7, 08 P-C205 a):
// the cell, whether it fits (0x5c9b40 without stacking: only the empty footprint) and the marks, as in the inventory.
// With the click and without fitting, false. If it fits, the click does nothing with 0x1c17..0x1c1a, 0x1a13, 0x1a14
// and the 0x1c0b of level 13 (0x5cf3c9..0x5cf40b); otherwise, the target (row · 8 + column) in 0x77f3cc8 and, from
// the inventory, the own OK at 0 and the C1 04 3C 00 in C3 (0x5cf438) before checking the lock; from the same grid,
// only the lock (0x5d07fa). With the lock released, the 24 to grid 1. For the other source grids, the original has no
// branch: they do not arrive (*inferred*). `inventoryOpen` and `panels` are not used.
[[nodiscard]] Core::Expected<bool, std::string> DropOnTheTradeGrid(const InventoryMoveContext& context,
                                                                   const GridDrop& drop);

// 0x5ca250(x + 15, y + 0x6e, 0x78ab0b8, 8, 4, 4), from 0x5dec19 with a personal shop open (CL9 milestone 7, 08 P-C208
// b): the cell, whether it fits (0x5c9b40 without stacking) and the marks, as in the trade. With the click and
// without fitting, false; if it fits, nothing with the untradable ones (0x5cf3c9..0x5cf40b) and otherwise
// (0x5d5046), the target (row · 8 + column + 0x4c) in 0x77f3cc8 and: from the inventory, with the own shop, the cursor
// item in 0x78a59c8 and the price dialog (0x5eba70(2)); from another grid, to the same slot 0x5be150 and otherwise,
// with the shop open, the C1 04 3F 03 in C3 and, with the lock released, the 24 from grid 4 to 4. `inventoryOpen` and
// `panels` are not used.
[[nodiscard]] Core::Expected<bool, std::string> DropOnTheShopGrid(const InventoryMoveContext& context,
                                                                  const GridDrop& drop);

// 0x5df449..0x5e070e, from 0x5d8d50 with the click outside the windows: what is not dropped on the ground (notice 269
// and 0x5be150), the boxes from 0x1a20 to 0x1a25 (notice 1915 and 0x5be150) and the jewel bundles and the Jewel of
// Guardian (0x402b30 and 0x5e06b6: 0x5be150, with notice 269 except with the inventory open and the mouse to its
// right). The rest (CL9 milestone 5): the seven window queries with the mouse over them, in their order (with
// 0x10000's rand() and 0x20's), the wizard's ring with the own record open (notice 923 with 80 or 40 by the level;
// level 3 is not dropped), the 0x1c1c (0x4379f0 and 0x437a20) and, with all that and the trade closed (another
// rand()), the terrain under the mouse: if there is any, the C3's two rand() and the C1 06 23 with its cell and the
// slot [0x78a6758], which stays in 0x6c82ec, and the mouse wait (0x77f3b10 at 0 and 0x6c8dcc at 6); otherwise,
// nothing. If it is not dropped, 0x5be150.
struct GroundDrop
{
    std::int32_t mouseX = 0;
    // IsOpen(0x4000000), which draws no rand().
    bool inventoryOpen = false;
    // The other queries of 0x659290 (0x5df5b6..0x5df6c2 and 0x5dfda8). Without it, what needs them stops.
    Ui::PanelRegistry* panels = nullptr;
};

[[nodiscard]] Core::Expected<void, std::string> DropOnTheGround(const InventoryMoveContext& context,
                                                                const GroundDrop& drop);

// CL9 milestone 5. Using the item of the inventory grid's cell `cell`, what the quick slots of 0x59ff10 (0x5a2bc8..,
// 0x5a35e0.., 0x5a3d64.. and 0x5a44eb..) and the grid (0x5c58c5.., 0x5c609b.. and 0x5c7418..) repeat: with the
// warehouse, 0x80 or the trade open (0x5af520, with the trade's rand()), notice 474 of type 2; otherwise, with
// 0x5767954 above 0, nothing; otherwise, 0x5767954 at 10, the C3's two rand(), the C1 06 26 with the cell + 12, 0 and
// [0x78abb26], and the sound by the cell's type (0x21 for 0x1c00 and 0x20 for 0x1c01..0x1c09).
[[nodiscard]] Core::Expected<void, std::string> UseInventoryItem(const InventoryMoveContext& context,
                                                                 Ui::PanelRegistry& panels, std::int32_t cell);

// The same with the 26's slot and target passed in, like 0x5c4110's right click on the books, the orbs and the 0x1a0b
// (slot = cell + 12, target 0) and the jewel on an item of 0x5ca250 (the jewel's slot, [0x78a6758], and the item's).
// The slot and the target give their low bytes. `where` is the address of the three queries, the one notice
// 474 carries.
struct InventoryUse
{
    std::int32_t slot = 0;
    std::int32_t target = 0;
    std::string_view where;
};

[[nodiscard]] Core::Expected<void, std::string> UseInventoryItemOn(const InventoryMoveContext& context,
                                                                   Ui::PanelRegistry& panels, const InventoryUse& use);

// 0x5bc740(0x78a9bb0, 8, 8, slot, item, 0) (0x5bcd0b.. and its tail): the item in its slot and, with the record open,
// the helpers without a pet, the raven without slot 1 and the cape without the level 3 ring. A type 0x1fff places
// nothing.
[[nodiscard]] Core::Expected<void, std::string> PlaceMovedItem(std::uint8_t slot, const Protocol::Items::ItemData& item,
                                                               const Scenes::WorldEntryResources& resources);

// 0x4d1400, the encrypted 24: releases the lock; with 0xff, 0x5be150; otherwise, the personal shops' prices
// (0x4d1b76..0x4d1c7d), the count at 0 and, in the inventory (0) or in the own trade grid (1, CL9 milestone 7),
// 0x5bc740 with the last argument at 0 and sound 0x1d. At the end, the 3D the lock kept (0x4d1d70).
[[nodiscard]] Core::Expected<void, std::string> ApplyItemMoveResult(const Protocol::Items::ItemMoveResult& result,
                                                                    const Scenes::WorldEntryResources& resources);
// 0x4c5ec0, the 28: 0x5bd750 with the slot and the end of the wait. From 0x4c (the personal shop), with 0x6c931c at 2
// in the copy of the own grid (0x78a6768); otherwise, with the cursor item lifted from that cell of 0x78ab0b8,
// 0x5be150 first.
[[nodiscard]] Core::Expected<void, std::string> ApplyItemDelete(const Protocol::Items::ItemDelete& deleted,
                                                                const Scenes::WorldEntryResources& resources);
// 0x4d3cc0, the 2A: the durability of the equipment slot, with the record open, or of the whole footprint in the
// grid, and the end of the wait.
[[nodiscard]] Core::Expected<void, std::string> ApplyDurability(const Protocol::Items::Durability& durability,
                                                                const Scenes::WorldEntryResources& resources);
// 0x4d3510, the 26, with the own record open (3848 rand() when closing it if left with no accesses): 0xfd, the end of
// the wait (0x5767954 at 0); 0xfe, the maximum life (+0x26); 0xff, the life (+0x22); another, the slot's potion: the
// durability (+0x1a) of the grid's cell slot − 12 drops by 1 if it is not 0 and, at 0, the cell is emptied (type
// 0xffff and +0x3c at 0). Outside the grid the original writes anyway: MuRet stops (0x4d35f6).
[[nodiscard]] Core::Expected<void, std::string> ApplyLifeRefill(const Protocol::Items::LifeRefill& refill,
                                                                Kernel::WorldState& world, Crt::CrtRandom& random);
// 0x4d3820, the 27, with the own record open: 0xfe, the maximum mana (+0x28) and the maximum BP (+0x3a); 0xff, the
// mana (+0x24) and the BP (+0x38); another, the mana and the slot's potion, like the 26 (0x4d3917). The 0xfd is not
// the end of the wait: it is one more slot.
[[nodiscard]] Core::Expected<void, std::string> ApplyManaRefill(const Protocol::Items::ManaRefill& refill,
                                                                Kernel::WorldState& world, Crt::CrtRandom& random);
// 0x4e31a3, the F3 14: the cursor count at 0, 0x5bc740 with the last argument at 0 and sound 0x45.
[[nodiscard]] Core::Expected<void, std::string> ApplyItemPut(const Protocol::Items::ItemPut& put,
                                                             const Scenes::WorldEntryResources& resources);
// 0x4d10d0, the 23 (CL9 milestone 5): with 0, 0x5be150; otherwise, with an equipment slot (under 12), with the own
// record open (3848 rand() when closing it with no accesses), the raven (0x421d60) if it is slot 1's 0x1a05, the type
// at 0xffff, +0x3c and +0x44 at 0, 0x512a30 and, in 8, the helpers (0x443d40); with another, 0x5bd750. Then the
// cursor count at 0. Always, 0x6c82ec at −1.
[[nodiscard]] Core::Expected<void, std::string> ApplyDropResult(const Protocol::Items::DropResult& result,
                                                                const Scenes::WorldEntryResources& resources);
// A short 23 (07 Q-C1240): of 0x4d10d0, the dropped slot (0x6c82ec) at −1, which it writes on every path, and with
// pkt[3] other than 0 the cursor emptied (0x7879fbc at 0, 0x4d13c7), also on every path that reads pkt[4].
void ApplyShortDropResult(const Protocol::Items::ShortDropResult& result, Kernel::WorldState& world);
// 0x4e316e, the F3 13: the key's character (0x5110d0) and, if it is a player, its equipment (0x5139f0, CL9.2): stops.
// If it is not, or with none, nothing.
[[nodiscard]] Core::Expected<void, std::string> ApplyEquipmentChange(const Protocol::Items::EquipmentChange& change,
                                                                     const Scenes::WorldEntryResources& resources);
} // namespace Client::Items
