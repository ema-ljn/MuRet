#include "Player/OwnStatHelpers.h"

#include "Core/Expected.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemTable.h"
#include "Actors/FullSetScan.h"
#include "Player/ItemSetOptionList.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/ItemState.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// 0x54ee00 with the golden folder's real Item.bmd byte +0x2d, through 0x5507e0, 0x550990 and 0x54f9b0 (06, "Los ocho
// helpers de los stats propios"). The expected values come from those bytes, read from the file: Small Shield (6 0)
// 22, Bronze Helm (7 0) 34, Wings of Elf (12 0) 200 and Skull Staff (5 0) 0. D+0x10 and D+0x12 are the same record's
// +0x28 and +0x29 (3 and 1, 9, 10), as 0x54bf40 copies them.
namespace
{
using Client::Kernel::ItemState;
using Client::Kernel::OwnCharacterRecord;
using Client::Player::ItemSetOptionList;
using Client::Tables::FirstItemModel;
using Client::Tables::ItemModel;

constexpr std::string_view ItemFile = "Local\\Item.bmd";
constexpr std::uint16_t EmptyType = 0xffff;

std::uint16_t TypeOf(std::uint8_t group, std::uint16_t index)
{
    return static_cast<std::uint16_t>(ItemModel(group, index) - FirstItemModel);
}

ItemState Worn(std::uint16_t type, std::uint8_t durability)
{
    ItemState item;
    item.id = type;
    item.durability = durability;
    return item;
}

OwnCharacterRecord DarkWizardWithAgility300()
{
    OwnCharacterRecord own;
    own.agility = 300;
    for (ItemState& item : own.equipment)
    {
        item.id = EmptyType;
    }
    return own;
}

class GoldenOwnStatHelpers : public Client::Tests::GoldenDataTest
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
};

// 0x5507e0: Small Shield at 2 of 22 (0.5f) adds 3 − 1 to A/3's 100; at 22 of 22 (0), the 3.
TEST_F(GoldenOwnStatHelpers, Word60WithTheRealSmallShield)
{
    const auto items = LoadItems();
    ASSERT_TRUE(items.has_value());
    OwnCharacterRecord own = DarkWizardWithAgility300();
    own.equipment[1] = Worn(TypeOf(6, 0), 2);
    own.equipment[1].byte10 = 3;

    ASSERT_TRUE(Client::Player::UpdateOwnWord60(own, &items->table).HasValue());
    EXPECT_EQ(own.word60, 102);

    own.equipment[1].durability = 22;
    ASSERT_TRUE(Client::Player::UpdateOwnWord60(own, &items->table).HasValue());
    EXPECT_EQ(own.word60, 103);
}

// 0x550990: to A/4's 75, Small Shield (2 of 22) 1 − 0, Bronze Helm (3 of 34) 9 − 4 and Wings of Elf (30 of 200)
// 10 − 5.
TEST_F(GoldenOwnStatHelpers, Word62WithARealEquipment)
{
    const auto items = LoadItems();
    ASSERT_TRUE(items.has_value());
    OwnCharacterRecord own = DarkWizardWithAgility300();
    own.equipment[1] = Worn(TypeOf(6, 0), 2);
    own.equipment[1].word12 = 1;
    own.equipment[2] = Worn(TypeOf(7, 0), 3);
    own.equipment[2].word12 = 9;
    own.equipment[7] = Worn(TypeOf(12, 0), 30);
    own.equipment[7].word12 = 10;
    ItemSetOptionList setOptions;

    ASSERT_TRUE(Client::Player::UpdateOwnWord62(own, &items->table, {}, &setOptions).HasValue());

    EXPECT_EQ(own.word62, 75 + 1 + 5 + 5);
}

// 0x54f9b0: Skull Staff has +0x2d at 0, 0x54ee00's maximum stays at 0 and the factor is 0: its 0x51 options add
// whole.
TEST_F(GoldenOwnStatHelpers, Words5aAnd5cWithTheRealSkullStaff)
{
    const auto items = LoadItems();
    ASSERT_TRUE(items.has_value());
    OwnCharacterRecord own = DarkWizardWithAgility300();
    own.equipment[0] = Worn(TypeOf(5, 0), 5);
    own.equipment[0].optionCount = 1;
    own.equipment[0].optionBytes[0] = 0x51;
    own.equipment[0].optionBytes[Client::Kernel::ItemOptionValueOffset] = 9;
    ItemSetOptionList setOptions;

    ASSERT_TRUE(Client::Player::UpdateOwnWords5aAnd5c(own, {}, &items->table, &setOptions).HasValue());

    EXPECT_EQ(own.word5a, 9);
    EXPECT_EQ(own.word5c, 9);
}
} // namespace
