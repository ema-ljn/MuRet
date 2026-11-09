#include "Scenes/OwnDerivedStats.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/ItemSetOptionList.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "World/ItemState.h"
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

// 0x40dc50 y la suma de 0x550d10 (0x550d49..0x550db1) con el Item.bmd real de la carpeta golden, y
// 0x550d10 hasta sus helpers con ItemSetType.bmd e ItemSetOption.bmd reales (06, "Los stats del
// registro propio"; 05, "Las tablas de los conjuntos"). Lo que se espera sale de los archivos, leídos
// aparte.
namespace
{
using Client::Data::Models::FirstItemModel;
using Client::Data::Models::ItemModel;
using Client::Scenes::OwnCharacterRecord;
using Client::World::ItemState;

constexpr std::string_view ItemFile = "Local\\Item.bmd";
constexpr std::string_view ItemSetTypeFile = "Local\\ItemSetType.bmd";
constexpr std::string_view ItemSetOptionFile = "Local\\ItemSetOption.bmd";

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

    // Las dos tablas de 0x40c600, con su checksum.
    std::optional<Client::Data::Tables::ItemSetTables> LoadItemSets()
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
        auto types = Client::Data::Tables::LoadItemSetTypeTable(std::as_bytes(std::span(*typeFile)));
        auto options = Client::Data::Tables::LoadItemSetOptionTable(std::as_bytes(std::span(*optionFile)));
        if (!types.HasValue() || !options.HasValue())
        {
            return std::nullopt;
        }
        return Client::Data::Tables::ItemSetTables{.types = std::move(types).Value(),
                                                   .options = std::move(options).Value()};
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
// Hyperion, el conjunto 3 de ItemSetOption.bmd, con sus tres piezas del byte 0 de ItemSetType.bmd: Bronze
// Armor (8 0), Bronze Pants (9 0) y Bronze Boots (11 0), con el byte +0x1c & 3 en 1. Con n = 2 entran los
// pares (2, 15) y (1, 15), y con +0x3c en 4 está completo: (0x13, 20) y (0x0c, 30), y el +0x358 del héroe
// en 1. 0x40da20 suma 15 a +0x2c (0x8d) y 15 a +0x30 (0x8e); 0x13 y 0x0c no son de ningún WORD de
// 0x550d10. La armadura con valor 1 suma 5 a +0x2e (el +0x44 de Item.bmd es 4). Sin marcas de clase, da
// lo mismo en todas las clases, con y sin evolución. Corta en los helpers, con el registro abierto por
// quien llama: ningún rand().
TEST_F(GoldenOwnDerivedStats, TheRealHyperionSetUntilTheHelpers)
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
        Client::Scenes::ItemSetState itemSets;
        Client::Scenes::ActorState hero;
        hero.characterClass = heroClass;
        std::uint8_t accesses = 1;
        Client::Render::Models::CrtRandom random(9);

        const auto recalculated = Client::Scenes::RecalculateOwnStats(own, {.itemSetTables = &*sets,
                                                                            .itemSets = &itemSets,
                                                                            .items = &items->table,
                                                                            .publishedHero = hero,
                                                                            .ownRecordAccesses = accesses,
                                                                            .random = random});

        ASSERT_FALSE(recalculated.HasValue());
        EXPECT_NE(recalculated.Error().find("0x54efe0"), std::string::npos);
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
        EXPECT_EQ(random.State(), Client::Render::Models::CrtRandom(9).State());
    }
}
} // namespace
