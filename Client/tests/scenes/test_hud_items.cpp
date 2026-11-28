#include "Items/HudItems.h"

#include "Tables/ItemTable.h"
#include "Kernel/OwnInventory.h"
#include "Kernel/ItemState.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// What the HUD reads from the items (CL8 milestone 2): the quick slots (0x558110 and 0x558490), the inventory's
// arrows (0x557d80), the maximum durability (0x5b1ba0) and the Item.bmd name.
namespace
{
using Client::Items::CountQuickSlotItems;
using Client::Items::CountTheAmmo;
using Client::Items::FindQuickSlotItem;
using Client::Items::ItemRecordName;
using Client::Items::MaxDurability;
using Client::Items::QuickSlots;
using Client::Kernel::InventoryCellCount;
using Client::Kernel::ItemState;

using Cells = std::array<ItemState, InventoryCellCount>;
using Record = std::array<std::byte, Client::Tables::ItemRecordSize>;

// The cell (row · 8 + column) with `type`, `count` in D+0x1a, the level in D+4 (bits 3 to 6) and +0x3c at 1.
void Put(Cells& cells, std::size_t row, std::size_t column, std::uint16_t type, std::uint8_t count = 1,
         std::uint32_t level = 0)
{
    ItemState& item = cells[row * 8 + column];
    item.id = type;
    item.durability = count;
    item.scalar1Low = level << 3;
    item.occupied = 1;
}

std::optional<std::size_t> Found(const QuickSlots& slots, const Cells& cells, int slot, bool blocked = false)
{
    const auto found = FindQuickSlotItem(slots, cells, slot, blocked);
    EXPECT_TRUE(found.HasValue()) << found.Error();
    return found.HasValue() ? found.Value() : std::nullopt;
}

std::int32_t Counted(const QuickSlots& slots, const Cells& cells, int slot)
{
    const auto counted = CountQuickSlotItems(slots, cells, slot);
    EXPECT_TRUE(counted.HasValue()) << counted.Error();
    return counted.HasValue() ? counted.Value() : -1;
}

// 0x5582c8..0x558376: the types from top to bottom and, in each one, columns 7 to 0 and rows 7 to 0. The 0x1c03 beats
// the 0x1c00 even though it is earlier in the grid; in the same column, the lower row.
TEST(HudItemsTest, TheQuickSlotWalksTheTypesDownAndTheGridFromTheLastColumn)
{
    Cells cells{};
    const QuickSlots slots;
    Put(cells, 0, 0, 0x1c00);
    Put(cells, 1, 1, 0x1c03);
    Put(cells, 6, 1, 0x1c03);
    EXPECT_EQ(Found(slots, cells, 0), 6u * 8 + 1);

    Put(cells, 0, 6, 0x1c03);
    EXPECT_EQ(Found(slots, cells, 0), 6u) << "la columna 6 va antes que la 1";

    // A cell with +0x3c at 0 does not count.
    for (ItemState& item : cells)
    {
        if (item.id == 0x1c03)
            item.occupied = 0;
    }
    EXPECT_EQ(Found(slots, cells, 0), 0u);
    cells[0].occupied = 0;
    EXPECT_EQ(Found(slots, cells, 0), std::nullopt);
}

// Each slot's ranges: the life one and the mana one cross, and slot 2 falls into the antidote with a type of no range.
TEST(HudItemsTest, EachSlotPicksItsRange)
{
    Cells cells{};
    Put(cells, 0, 0, 0x1c01);
    Put(cells, 0, 1, 0x1c05);
    Put(cells, 0, 2, 0x1c08);
    QuickSlots slots;
    slots.types = {0x1c05, 0x1c02, 0x1234};
    EXPECT_EQ(Found(slots, cells, 0), 1u);
    EXPECT_EQ(Found(slots, cells, 1), 0u);
    EXPECT_EQ(Found(slots, cells, 2), 2u);
    // With a type from another range, 0 and 1 go back to their own.
    slots.types = {0x1234, 0x1234, 0x1c00};
    EXPECT_EQ(Found(slots, cells, 0), 0u);
    EXPECT_EQ(Found(slots, cells, 1), 1u);
    EXPECT_EQ(Found(slots, cells, 2), 0u);
}

// 0x1c07, 0x1c0a and 0x1c14 ask for the slot's level ((D+4 >> 3) & 0xf); with the request locked (0x576794c), the
// 0x1c0a is not searched.
TEST(HudItemsTest, TheLevelledTypesAskForTheLevelOfTheSlot)
{
    Cells cells{};
    QuickSlots slots;
    slots.types = {0x1c07, 0x1c0a, 0x1c14};
    slots.levels = {2, 1, 0};
    Put(cells, 0, 7, 0x1c07, 1, 1);
    Put(cells, 0, 3, 0x1c07, 1, 2);
    Put(cells, 1, 7, 0x1c0a, 1, 0);
    Put(cells, 1, 2, 0x1c0a, 1, 1);
    Put(cells, 2, 7, 0x1c14, 1, 3);
    Put(cells, 2, 1, 0x1c14, 1, 0);
    EXPECT_EQ(Found(slots, cells, 0), 3u);
    EXPECT_EQ(Found(slots, cells, 1), 8u + 2);
    EXPECT_EQ(Found(slots, cells, 2), 16u + 1);
    EXPECT_EQ(Found(slots, cells, 1, true), std::nullopt);
    EXPECT_EQ(Found(slots, cells, 0, true), 3u);
}

// A 0x1c14 with a level leaves the range in uninitialized locals (0x558191 and 0x5584ec): the error, with the address.
TEST(HudItemsTest, ALevelled1c14HasNoRange)
{
    const Cells cells{};
    QuickSlots slots;
    slots.types = {0x1c14, 0x1c00, 0x1c00};
    slots.levels = {1, 0, 0};
    const auto found = FindQuickSlotItem(slots, cells, 0, false);
    ASSERT_FALSE(found.HasValue());
    EXPECT_NE(found.Error().find("0x558110"), std::string::npos) << found.Error();
    const auto counted = CountQuickSlotItems(slots, cells, 0);
    ASSERT_FALSE(counted.HasValue());
    EXPECT_NE(counted.Error().find("0x558490"), std::string::npos) << counted.Error();
}

// 0x558119..0x558141 (CL9 milestone 4): the skills ask for the mana potions with 3 (0x1c04 to 0x1c06) and the life
// ones with 4 (0x1c00 to 0x1c03), without looking at the slots, their level or 0x576794c; with 5 the range comes from
// uninitialized locals.
TEST(HudItemsTest, TheSkillsAskForTheManaAndLifePotionsWith3And4)
{
    Cells cells{};
    Put(cells, 0, 0, 0x1c04);
    Put(cells, 7, 1, 0x1c05);
    Put(cells, 1, 6, 0x1c02);
    Put(cells, 2, 2, 0x1c00);
    Put(cells, 3, 3, 0x1c08);
    QuickSlots slots;
    slots.types = {0x1c14, 0x1c0a, 0x1c08};
    slots.levels = {3, 3, 3};
    // 0x1c05 beats 0x1c04 and 0x1c02 beats 0x1c00, even though they are later in the grid.
    EXPECT_EQ(Found(slots, cells, Client::Items::ManaPotionSlot, true), 7u * 8u + 1u);
    EXPECT_EQ(Found(slots, cells, Client::Items::LifePotionSlot, true), 1u * 8u + 6u);
    cells[7 * 8 + 1].occupied = 0;
    EXPECT_EQ(Found(slots, cells, Client::Items::ManaPotionSlot), 0u);
    const Cells empty{};
    EXPECT_EQ(Found(slots, empty, Client::Items::ManaPotionSlot), std::nullopt);
    EXPECT_EQ(Found(slots, empty, Client::Items::LifePotionSlot), std::nullopt);
    const auto fifth = FindQuickSlotItem(slots, cells, 5, false);
    ASSERT_FALSE(fifth.HasValue());
    EXPECT_NE(fifth.Error().find("0x558110(5)"), std::string::npos) << fifth.Error();
}

// 0x558490: the potions add up D+0x1a over their whole range; 0x1c09, 0x1c0a and 0x1c14 count cells.
TEST(HudItemsTest, TheCountAddsThePotionsAndCountsTheOthers)
{
    Cells cells{};
    Put(cells, 0, 0, 0x1c00, 3);
    Put(cells, 0, 1, 0x1c02, 5);
    Put(cells, 0, 2, 0x1c04, 7);
    Put(cells, 0, 3, 0x1c09, 9);
    Put(cells, 0, 4, 0x1c09, 9);
    Put(cells, 0, 5, 0x1c08, 4);
    QuickSlots slots;
    EXPECT_EQ(Counted(slots, cells, 0), 8);
    EXPECT_EQ(Counted(slots, cells, 1), 7);
    EXPECT_EQ(Counted(slots, cells, 2), 4);
    slots.types[2] = 0x1c09;
    EXPECT_EQ(Counted(slots, cells, 2), 2);
}

// 0x557d80: only the elf (class & 7 at 2); the left bow asks for arrows (0x80f) and the right crossbow for bolts
// (0x807), and it counts cells.
TEST(HudItemsTest, TheAmmoIsTheElfsArrowsOrBolts)
{
    Cells cells{};
    Put(cells, 0, 0, 0x80f, 100);
    Put(cells, 0, 1, 0x80f, 100);
    Put(cells, 0, 2, 0x807, 50);
    cells[3] = cells[0];
    cells[3].occupied = 0;
    EXPECT_EQ(CountTheAmmo(2, 0, 0x800, cells), 2);
    EXPECT_EQ(CountTheAmmo(0x12, 0, 0x815, cells), 2) << "la clase es (+0xb & 7)";
    EXPECT_EQ(CountTheAmmo(2, 0x808, 0, cells), 1);
    EXPECT_EQ(CountTheAmmo(2, 0x80f, 0x80f, cells), 0) << "sin arco ni ballesta";
    EXPECT_EQ(CountTheAmmo(1, 0, 0x800, cells), 0);
}

// 0x5b1ba0: Item.bmd's durability (+0x2d, or +0x2e for the staffs) plus 1, 2, 3, 4, 5 or 6 per level, and 20 for
// ancient or 15 for excellent.
TEST(HudItemsTest, TheMaxDurabilityGrowsWithTheLevelAndTheOptions)
{
    Record record{};
    record[0x2d] = std::byte{20};
    record[0x2e] = std::byte{30};
    ItemState item;
    item.id = 0;
    EXPECT_EQ(MaxDurability(item, record, 0), 20);
    EXPECT_EQ(MaxDurability(item, record, 4), 24);
    EXPECT_EQ(MaxDurability(item, record, 9), 34);
    EXPECT_EQ(MaxDurability(item, record, 13), 20 + 4 + 10 + 3 + 4 + 5 + 6);
    item.id = 0xa00;
    EXPECT_EQ(MaxDurability(item, record, 0), 30);

    item.id = 0;
    item.flags1b = 1;
    EXPECT_EQ(MaxDurability(item, record, 0), 35);
    item.flags1c = 2;
    EXPECT_EQ(MaxDurability(item, record, 0), 40) << "el ancestral va antes que el excelente";
    item.flags1c = 3;
    EXPECT_EQ(MaxDurability(item, record, 0), 35);
    item.flags1c = 0;
    constexpr std::array<std::uint16_t, 7> Excluded{0x13, 0x812, 0xa0a, 0x1a1e, 0x40d, 0x1803, 0x1806};
    for (const std::uint16_t excluded : Excluded)
    {
        item.id = excluded;
        EXPECT_EQ(MaxDurability(item, record, 0), excluded == 0xa0a ? 30 : 20) << excluded;
    }
}

// Item.bmd's "%s": up to the first '\0', or the 0x54 bytes.
TEST(HudItemsTest, TheNameStopsAtTheFirstZero)
{
    Record record{};
    constexpr std::string_view Name = "Kris";
    for (std::size_t i = 0; i < Name.size(); ++i)
        record[i] = static_cast<std::byte>(Name[i]);
    record[Name.size() + 1] = std::byte{'x'};
    EXPECT_EQ(ItemRecordName(record), Name);
    record.fill(std::byte{'a'});
    EXPECT_EQ(ItemRecordName(record), std::string(record.size(), 'a'));
}
} // namespace
