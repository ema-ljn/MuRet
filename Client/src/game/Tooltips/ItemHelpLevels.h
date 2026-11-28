#pragma once

#include "Tables/ItemTable.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Hud
{
struct GameHudContext;
} // namespace Client::Hud

// The item help's per-level table (06 § HUD y ventanas, "La ayuda del ítem"): 0x5afc00 fills it at 0x787a15c (14
// levels of 0x44 bytes, 17 dwords each) and keeps it for the item at 0x6c9334. It is read by the tail of 0x5b0550 (the
// box width and which columns it draws) and by 0x5af9b0 (one column in the lines).
namespace Client::Tooltips
{

inline constexpr std::size_t ItemHelpLevelCount = 14;
inline constexpr std::size_t ItemHelpColumnCount = 17;
// 0x6c9334 (.data −1)
inline constexpr std::int32_t NoItemHelpTable = -1;

// Each column's dword, 0x787a15c + 0x44 · level + 4 · column. Nobody writes 11, 12 and 16: they stay at 0, like the
// .bss.
enum ItemHelpColumn : std::size_t
{
    ItemHelpLevel = 0,
    // 1 if the own record meets the level's requirements, 0 if not.
    ItemHelpUsable = 1,
    ItemHelpMinDamage = 2,     // Item.bmd+0x26
    ItemHelpMaxDamage = 3,     // +0x27
    ItemHelpMagic = 4,         // +0x2f, except from 0x408 to 0x40d
    ItemHelpScepterMagic = 5,  // +0x2f, from 0x408 to 0x40d
    ItemHelpDefense = 6,       // +0x29
    ItemHelpDefenseRate = 7,   // +0x28
    ItemHelpStrength = 8,      // +0x30
    ItemHelpAgility = 9,       // +0x32
    ItemHelpEnergy = 10,       // +0x34
    ItemHelpLeadership = 13,   // +0x38
    ItemHelpVitality = 14,     // +0x36
    ItemHelpRequiredLevel = 15 // +0x3a
};

using ItemHelpRow = std::array<std::int32_t, ItemHelpColumnCount>;

struct ItemHelpLevels
{
    // 0x787a15c: one row per level, from 0 to 13.
    std::array<ItemHelpRow, ItemHelpLevelCount> rows{};
    // 0x6c9334: the table's item. Nobody else writes it: neither closing the help nor changing the stats refill it for
    // the same item (Q-C750).
    std::int32_t item = NoItemHelpTable;
};

// 0x5afc00(item): with the table's item, nothing. Otherwise, it keeps it and fills the 14 levels with the item's
// Item.bmd record (`record`, the one of 0x77f3b0c): each byte of +0x26, +0x27, +0x28 and +0x29 (non-zero) plus 3 ·
// min(level, 9) and, from 10 to 13, 4, 9, 15 and 22 more (the shield, from 0xc00 to 0xdff, adds only the level to
// +0x29); +0x2f the same, halved and, except from 0x408 to 0x40d, plus 2 · level; each non-zero requirement from +0x30
// to +0x38, (Item.bmd+0x20 + 3 · level) · requirement · 3 (4 for energy) / 100 + 20, in signed 32 bits; and +0x3a as
// is. Then, with the own record open (3848 rand() on closing it if it is left with no accesses, one per level),
// whether it meets the requirements (the stats plus the items', in 16 bits, and the level).
void FillTheItemHelpLevels(Hud::GameHudContext& context, std::int32_t item,
                           std::span<const std::byte, Tables::ItemRecordSize> record);
} // namespace Client::Tooltips
