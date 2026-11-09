#include "Scenes/OwnStatHelpers.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemTable.h"
#include "Scenes/ItemSetOptionList.h"
#include "Scenes/OwnCharacterRecord.h"
#include "World/ItemState.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// 0x54ee00 con el byte +0x2d del Item.bmd real de la carpeta golden, a través de 0x5507e0, 0x550990 y
// 0x54f9b0 (06, "Los ocho helpers de los stats propios"). Lo que se espera sale de esos bytes, leídos del
// archivo: Small Shield (6 0) 22, Bronze Helm (7 0) 34, Wings of Elf (12 0) 200 y Skull Staff (5 0) 0. Los
// D+0x10 y D+0x12 son los +0x28 y +0x29 del mismo registro (3 y 1, 9, 10), como los copia 0x54bf40.
namespace
{
using Client::Data::Models::FirstItemModel;
using Client::Data::Models::ItemModel;
using Client::Scenes::ItemSetOptionList;
using Client::Scenes::OwnCharacterRecord;
using Client::World::ItemState;

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
    std::optional<Client::Data::Tables::LoadedItemTable> LoadItems()
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
        auto loaded = Client::Data::Tables::LoadItemTable(std::as_bytes(std::span(*file)));
        if (!loaded.HasValue())
        {
            return std::nullopt;
        }
        return std::move(loaded).Value();
    }
};

// 0x5507e0: Small Shield con 2 de 22 (0.5f) suma 3 − 1 a los 100 de A/3; con 22 de 22 (0), los 3.
TEST_F(GoldenOwnStatHelpers, Word60WithTheRealSmallShield)
{
    const auto items = LoadItems();
    ASSERT_TRUE(items.has_value());
    OwnCharacterRecord own = DarkWizardWithAgility300();
    own.equipment[1] = Worn(TypeOf(6, 0), 2);
    own.equipment[1].byte10 = 3;

    ASSERT_TRUE(Client::Scenes::UpdateOwnWord60(own, &items->table).HasValue());
    EXPECT_EQ(own.word60, 102);

    own.equipment[1].durability = 22;
    ASSERT_TRUE(Client::Scenes::UpdateOwnWord60(own, &items->table).HasValue());
    EXPECT_EQ(own.word60, 103);
}

// 0x550990: a los 75 de A/4, Small Shield (2 de 22) 1 − 0, Bronze Helm (3 de 34) 9 − 4 y Wings of Elf (30
// de 200) 10 − 5.
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

    ASSERT_TRUE(Client::Scenes::UpdateOwnWord62(own, &items->table, {}, &setOptions).HasValue());

    EXPECT_EQ(own.word62, 75 + 1 + 5 + 5);
}

// 0x54f9b0: Skull Staff tiene el +0x2d en 0, el máximo de 0x54ee00 queda en 0 y el factor es 0: sus 0x51
// suman enteras.
TEST_F(GoldenOwnStatHelpers, Words5aAnd5cWithTheRealSkullStaff)
{
    const auto items = LoadItems();
    ASSERT_TRUE(items.has_value());
    OwnCharacterRecord own = DarkWizardWithAgility300();
    own.equipment[0] = Worn(TypeOf(5, 0), 5);
    own.equipment[0].optionCount = 1;
    own.equipment[0].optionBytes[0] = 0x51;
    own.equipment[0].optionBytes[Client::World::ItemOptionValueOffset] = 9;
    ItemSetOptionList setOptions;

    ASSERT_TRUE(Client::Scenes::UpdateOwnWords5aAnd5c(own, {}, &items->table, &setOptions).HasValue());

    EXPECT_EQ(own.word5a, 9);
    EXPECT_EQ(own.word5c, 9);
}
} // namespace
