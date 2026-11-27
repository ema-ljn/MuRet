#include "Player/OwnDerivedStats.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Kernel/ActorState.h"
#include "Player/ItemSetOptionList.h"
#include "Player/ItemSetState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/ItemState.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// 0x40dc50 and 0x550d10's sum (0x550d49..0x550db1) with the golden folder's real Item.bmd, and 0x550d10 up to its
// helpers with the real ItemSetType.bmd and ItemSetOption.bmd (06, "Los stats del registro propio"; 05, "Las tablas de
// los conjuntos"). The expected values come from the files, read separately.
namespace
{
using Client::Kernel::ItemState;
using Client::Kernel::OwnCharacterRecord;
using Client::Tables::FirstItemModel;
using Client::Tables::ItemModel;

constexpr std::string_view ItemFile = "Local\\Item.bmd";
constexpr std::string_view ItemSetTypeFile = "Local\\ItemSetType.bmd";
constexpr std::string_view ItemSetOptionFile = "Local\\ItemSetOption.bmd";

// An item's type, its record's index: the model without the 0x212.
std::uint16_t TypeOf(std::uint8_t group, std::uint16_t index)
{
    return static_cast<std::uint16_t>(ItemModel(group, index) - FirstItemModel);
}

ItemState Worn(std::uint16_t type, std::uint8_t value)
{
    ItemState item;
    item.id = type;
    item.durability = 1;
    item.flags1c = static_cast<std::uint8_t>(value << 2);
    return item;
}

class GoldenOwnDerivedStats : public Client::Tests::GoldenDataTest
{
protected:
    std::optional<Client::Tables::LoadedItemTable> LoadItems()
    {
        const auto path = DataFile(ItemFile);
        if (!path)
        {
            return std::nullopt;
        }
        const std::optional<std::vector<char>> file = Client::Tests::ReadFileBytes(*path);
        if (!file)
        {
            return std::nullopt;
        }
        auto loaded = Client::Tables::LoadItemTable(std::as_bytes(std::span(*file)));
        if (!loaded.HasValue())
        {
            return std::nullopt;
        }
        return std::move(loaded).Value();
    }

    // 0x40c600's two tables, with their checksum.
    std::optional<Client::Tables::ItemSetTables> LoadItemSets()
    {
        const auto typePath = DataFile(ItemSetTypeFile);
        const auto optionPath = DataFile(ItemSetOptionFile);
        if (!typePath || !optionPath)
        {
            return std::nullopt;
        }
        const std::optional<std::vector<char>> typeFile = Client::Tests::ReadFileBytes(*typePath);
        const std::optional<std::vector<char>> optionFile = Client::Tests::ReadFileBytes(*optionPath);
        if (!typeFile || !optionFile)
        {
            return std::nullopt;
        }
        auto types = Client::Tables::LoadItemSetTypeTable(std::as_bytes(std::span(*typeFile)));
        auto options = Client::Tables::LoadItemSetOptionTable(std::as_bytes(std::span(*optionFile)));
        if (!types.HasValue() || !options.HasValue())
        {
            return std::nullopt;
        }
        return Client::Tables::ItemSetTables{.types = std::move(types).Value(), .options = std::move(options).Value()};
    }
};

// In the real Item.bmd, +0x44 is 1 in Kris (0 0), 2 in Short Bow (4 0), 3 in Skull Staff (5 0), 4 in Small Shield
// (6 0), and 0 in Wings of Elf (12 0) and in Cape of Lord (13 30).
TEST_F(GoldenOwnDerivedStats, TheKindIsByte44OfTheRealItemBmd)
{
    const auto items = LoadItems();
    ASSERT_TRUE(items.has_value());
    struct Case
    {
        std::uint8_t group;
        std::uint16_t index;
        std::uint8_t kind;
    };
    for (const Case& item :
         {Case{0, 0, 1}, Case{4, 0, 2}, Case{5, 0, 3}, Case{6, 0, 4}, Case{12, 0, 0}, Case{13, 30, 0}})
    {
        SCOPED_TRACE(TypeOf(item.group, item.index));
        const auto bonus = Client::Player::ItemStatBonusOf(Worn(TypeOf(item.group, item.index), 2), items->table);
        ASSERT_TRUE(bonus.HasValue());
        EXPECT_EQ(bonus.Value().kind, item.kind);
        EXPECT_EQ(bonus.Value().value, 2);
    }
}

// An equipment with real Item.bmd items: Short Bow with 2 adds 10 to +0x2c, Small Shield with 3 and Bronze Helm (7 0,
// also 4) with 1 add 20 to +0x2e, Pendant of Lightning (13 12, 3) in place 9 adds 5 to +0x30, and Wings of Elf (0)
// adds nothing.
TEST_F(GoldenOwnDerivedStats, TheBonusesOfARealEquipment)
{
    const auto items = LoadItems();
    ASSERT_TRUE(items.has_value());
    OwnCharacterRecord own;
    for (ItemState& item : own.equipment)
    {
        item.id = 0xffff;
    }
    own.equipment[0] = Worn(TypeOf(4, 0), 2);
    own.equipment[1] = Worn(TypeOf(6, 0), 3);
    own.equipment[2] = Worn(TypeOf(7, 0), 1);
    own.equipment[7] = Worn(TypeOf(12, 0), 3);
    own.equipment[9] = Worn(TypeOf(13, 12), 1);

    ASSERT_TRUE(Client::Player::AddOwnItemStatBonuses(own, &items->table).HasValue());

    EXPECT_EQ(own.word2a, 0);
    EXPECT_EQ(own.word2c, 10);
    EXPECT_EQ(own.word2e, 20);
    EXPECT_EQ(own.word30, 5);
}
// Hyperion, ItemSetOption.bmd's set 3, with its three pieces of ItemSetType.bmd's byte 0: Bronze Armor (8 0), Bronze
// Pants (9 0) and Bronze Boots (11 0), with byte +0x1c & 3 at 1. With n = 2 the pairs (2, 15) and (1, 15) apply, and
// with +0x3c at 4 it is complete: (0x13, 20) and (0x0c, 30), and the hero's +0x358 at 1. 0x40da20 adds 15 to +0x2c
// (0x8d) and 15 to +0x30 (0x8e); 0x13 and 0x0c belong to no WORD of 0x550d10. The armor with value 1 adds 5 to +0x2e
// (Item.bmd's +0x44 is 4). With no class marks, it is the same for every class, with or without evolution. It stops at
// 0x550990, without knowing what 0x505820 left (scan505820 at nullopt), with the record opened by the caller: no
// rand().
TEST_F(GoldenOwnDerivedStats, TheRealHyperionSetUntil0x550990)
{
    const auto items = LoadItems();
    const auto sets = LoadItemSets();
    ASSERT_TRUE(items.has_value());
    ASSERT_TRUE(sets.has_value());
    for (std::uint8_t heroClass = 0; heroClass < 16; ++heroClass)
    {
        SCOPED_TRACE(int{heroClass});
        OwnCharacterRecord own;
        for (ItemState& item : own.equipment)
        {
            item.id = 0xffff;
        }
        own.equipment[3] = Worn(TypeOf(8, 0), 1);
        own.equipment[4] = Worn(TypeOf(9, 0), 0);
        own.equipment[6] = Worn(TypeOf(11, 0), 0);
        for (const std::size_t slot : {std::size_t{3}, std::size_t{4}, std::size_t{6}})
        {
            own.equipment[slot].flags1c = static_cast<std::uint8_t>(own.equipment[slot].flags1c | 1u);
        }
        Client::Player::ItemSetState itemSets;
        Client::Kernel::ActorState hero;
        hero.characterClass = heroClass;
        std::uint8_t accesses = 1;
        Client::Crt::CrtRandom random(9);

        const auto recalculated = Client::Player::RecalculateOwnStats(own, {.itemSetTables = &*sets,
                                                                            .itemSets = &itemSets,
                                                                            .items = &items->table,
                                                                            .publishedHero = hero,
                                                                            .ownRecordAccesses = accesses,
                                                                            .random = random});

        ASSERT_FALSE(recalculated.HasValue());
        EXPECT_NE(recalculated.Error().find("0x550990"), std::string::npos);
        EXPECT_EQ(std::string(itemSets.names[0].data()), "Hyperion");
        EXPECT_EQ(itemSets.list.setIds[0], 3);
        EXPECT_EQ(itemSets.list.count9348, 4);
        EXPECT_EQ(itemSets.list.count9349, 0);
        EXPECT_EQ(itemSets.list.entries[2][0], 0x13);
        EXPECT_EQ(itemSets.list.entries[3][0], 0x0c);
        EXPECT_EQ(itemSets.list.entries[0][1], 0);
        EXPECT_EQ(itemSets.list.values[3], 30);
        EXPECT_EQ(itemSets.byte9307, 0);
        EXPECT_EQ(hero.byte358, 1);
        EXPECT_EQ(own.word2a, 0);
        EXPECT_EQ(own.word2c, 15);
        EXPECT_EQ(own.word30, 15);
        EXPECT_EQ(own.word2e, 5);
        EXPECT_EQ(own.word36, 0);
        EXPECT_EQ(random.State(), Client::Crt::CrtRandom(9).State());
    }
}
} // namespace
