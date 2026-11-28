#pragma once

#include "Core/Expected.h"
#include "Tables/ItemTable.h"
#include "Kernel/OwnInventory.h"
#include "Kernel/ItemState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

// What the game HUD (CL8 milestone 2) reads from the items: the quick slots (Q, W and E: 0x558110 and 0x558490), the
// inventory's arrow or bolt count (0x557d80) and an item's maximum durability (0x5b1ba0). The grids are the 64 cells
// of 0x78a9bb0, which are not in the protection table: walking them draws no rand().
namespace Client::Items
{
// 0x6c8e40, 0x6c8e44 and 0x6c8e48 (.data 0x1c00, 0x1c04 and 0x1c08: the small life potion, the small mana potion and
// the antidote) and their levels in 0x77f3b34, 0x77f3b38 and 0x77f3b3c (.bss 0). The F3 30 changes them (0x4dd570,
// milestone 3).
struct QuickSlots
{
    std::array<std::int32_t, 3> types{0x1c00, 0x1c04, 0x1c08};
    std::array<std::int32_t, 3> levels{};
};

// 0x558110(i): the inventory cell (row · 8 + column) of the item the slot `slot` uses, or empty. With i from 0 to 2,
// the slot's type picks a range of types: 0x1c08 to 0x1c0a and 0x1c14 (slot 2 from 0x1c09) are themselves, 0x1c07
// too, those from 0x1c00 to 0x1c03 and from 0x1c04 to 0x1c06 go together, and any other falls in the slot's own
// range (0 and 1: life's and mana's, 2: 0x1c08). With 3 (0x558120, the skills without mana: ManaPotionSlot) the mana
// ones, 0x1c04 to 0x1c06, and with 4 (0x558134) the life ones, 0x1c00 to 0x1c03, without looking at the slots or
// 0x576794c (0x55815d jumps to 0x5582c1); their level, [0x77f3b40] and [0x77f3b44], is not read with those types.
// With another i, the range comes from uninitialized locals: the error. It walks the types from top to bottom and,
// in each one, columns 7 to 0 and in each column rows 7 to 0: the first cell with that type and +0x3c greater than
// 0 wins, unless its level ((D+4 >> 3) & 0xf) is not the slot's for types 0x1c14, 0x1c0a and 0x1c07. With
// `requestBlocked` (0x576794c) and the slot at 0x1c0a, empty (0x5582c1). A 0x1c14 with a level other than 0 walks
// from the slot number up to an uninitialized local (0x558191, 0x558221 and 0x55827f): the error.
inline constexpr int ManaPotionSlot = 3;
inline constexpr int LifePotionSlot = 4;

[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
FindQuickSlotItem(const QuickSlots& slots, std::span<const Kernel::ItemState, Kernel::InventoryCellCount> cells,
                  int slot, bool requestBlocked);

// 0x558490(i) with i from 0 to 2: how many there are for the slot `slot`, with the same ranges and the same walk as
// FindQuickSlotItem, except that slot 2 with a type outside the ranges falls in 0x1c08 and that it does not look at
// 0x576794c. 0x1c09, 0x1c0a and 0x1c14 count cells; the others add up the durability (D+0x1a), which in the potions
// is the amount. A 0x1c14 with a level is the same error (0x5584ec, 0x55858b and 0x5585fa).
[[nodiscard]] Core::Expected<std::int32_t, std::string>
CountQuickSlotItems(const QuickSlots& slots, std::span<const Kernel::ItemState, Kernel::InventoryCellCount> cells,
                    int slot);

// 0x557d80 after reading the own record: with the class (+0xb & 7) other than 2 (the elf), 0. Otherwise, the type of
// the arrows or bolts the weapon asks for: a bow on the left (0x800 to 0x806, 0x811, 0x814 or 0x815), the arrows
// (0x80f); otherwise, a crossbow on the right (0x808 to 0x80e or 0x810 to 0x9ff), the bolts (0x807); otherwise, none.
// It counts the inventory cells with that type and +0x3c greater than 0.
[[nodiscard]] std::int32_t CountTheAmmo(std::uint8_t characterClass, std::uint16_t rightHandType,
                                        std::uint16_t leftHandType,
                                        std::span<const Kernel::ItemState, Kernel::InventoryCellCount> cells);

// 0x5b1ba0(item, Item.bmd record, level): Item.bmd's durability (+0x2d, or +0x2e from 0xa00 to 0xbff) plus what each
// level adds (1 up to 4, 2 up to 9, and 3, 4, 5 and 6 at 9, 10, 11 and from 12 on), plus 20 with D+0x1c & 3 at 1 or
// 2, or 15 with any excellent option (D+0x1b & 0x3f) outside 0x1803..0x1806, 0x13, 0x812, 0xa0a, 0x1a1e and 0x40d.
// In 16 bits, like the ax it returns.
[[nodiscard]] std::uint16_t MaxDurability(const Kernel::ItemState& item,
                                          std::span<const std::byte, Tables::ItemRecordSize> record, int level);

// Its bytes from +0 up to the first '\0' (or the 0x54), as the original's "%s" reads it.
[[nodiscard]] std::string ItemRecordName(std::span<const std::byte, Tables::ItemRecordSize> record);
} // namespace Client::Items
