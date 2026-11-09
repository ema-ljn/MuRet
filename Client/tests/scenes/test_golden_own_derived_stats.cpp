#include "Scenes/OwnDerivedStats.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemTable.h"
#include "Scenes/OwnCharacterRecord.h"
#include "World/ItemState.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// 0x40dc50 y la suma de 0x550d10 (0x550d49..0x550db1) con el Item.bmd real de la carpeta golden (06, "Los
// stats del registro propio"). Lo que se espera es el byte +0x44 de cada registro, leído del archivo.
namespace
{
using Client::Data::Models::FirstItemModel;
using Client::Data::Models::ItemModel;
using Client::Scenes::OwnCharacterRecord;
using Client::World::ItemState;

constexpr std::string_view ItemFile = "Local\\Item.bmd";

// El tipo de un ítem, el índice de su registro: el modelo sin el 0x212.
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

// En el Item.bmd real, el +0x44 vale 1 en Kris (0 0), 2 en Short Bow (4 0), 3 en Skull Staff (5 0), 4 en
// Small Shield (6 0), y 0 en Wings of Elf (12 0) y en Cape of Lord (13 30).
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
        const auto bonus = Client::Scenes::ItemStatBonusOf(Worn(TypeOf(item.group, item.index), 2), items->table);
        ASSERT_TRUE(bonus.HasValue());
        EXPECT_EQ(bonus.Value().kind, item.kind);
        EXPECT_EQ(bonus.Value().value, 2);
    }
}

// Un equipo con ítems del Item.bmd real: Short Bow con 2 suma 10 a +0x2c, Small Shield con 3 y Bronze Helm
// (7 0, también 4) con 1 suman 20 a +0x2e, Pendant of Lightning (13 12, 3) en el lugar 9 suma 5 a +0x30,
// y Wings of Elf (0) no suma.
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

    ASSERT_TRUE(Client::Scenes::AddOwnItemStatBonuses(own, &items->table).HasValue());

    EXPECT_EQ(own.word2a, 0);
    EXPECT_EQ(own.word2c, 10);
    EXPECT_EQ(own.word2e, 20);
    EXPECT_EQ(own.word30, 5);
}
} // namespace
