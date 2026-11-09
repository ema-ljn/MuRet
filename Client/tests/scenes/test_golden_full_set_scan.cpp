#include "Scenes/FullSetScan.h"

#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemTable.h"
#include "Render/Models/Cloth.h"
#include "Scenes/OwnCharacterRecord.h"
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

// 0x505aa7..0x505ac1 con el Item.bmd real de la carpeta golden (06, "El conjunto completo de 0x505820"). Las
// cuatro armaduras con las que [0x743a17c] queda en 1 son, de las 512 del grupo 8, las que tienen nombre y
// no tienen casco con nombre en el mismo índice del grupo 7: los conjuntos sin casco, que la clase 3 viste
// sin él (06 lo da como *inferido*). El nombre es el comienzo del registro: sin nombre, su primer byte es 0.
namespace
{
using Client::Data::Models::FirstItemModel;
using Client::Data::Models::ItemModel;

constexpr std::string_view ItemFile = "Local\\Item.bmd";
constexpr std::uint8_t MagicGladiator = 3;
constexpr std::uint8_t HelmGroup = 7;
constexpr std::uint8_t ArmorGroup = 8;
constexpr std::size_t OwnArmorSlot = 3;

class GoldenFullSetScan : public Client::Tests::GoldenDataTest
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

bool IsNamed(const Client::Data::Tables::ItemTable& items, std::uint8_t group, std::uint16_t index)
{
    const std::size_t type = static_cast<std::size_t>(ItemModel(group, index) - FirstItemModel);
    const auto record = items.Record(type);
    return record.has_value() && (*record)[0] != std::byte{0};
}

TEST_F(GoldenFullSetScan, TheArmorsThatKeepTheBonusAreTheNamedOnesWithoutAHelm)
{
    const auto items = LoadItems();
    ASSERT_TRUE(items.has_value());
    int kept = 0;
    for (std::uint16_t index = 0; index < Client::Data::Models::ItemModelsPerGroup; ++index)
    {
        SCOPED_TRACE(index);
        // La clase 3 con el conjunto +13 de ese índice, sin casco, y su armadura en el registro.
        Client::Scenes::ScannedCharacter character{.characterClass = MagicGladiator};
        character.body[0].model = 0xffff;
        for (std::size_t part = 1; part < character.body.size(); ++part)
        {
            character.body[part] = {.model = ItemModel(static_cast<std::uint8_t>(HelmGroup + part), index),
                                    .level = 13};
        }
        Client::Scenes::OwnCharacterRecord own;
        own.equipment[OwnArmorSlot].id = static_cast<std::uint16_t>(ItemModel(ArmorGroup, index) - FirstItemModel);
        std::uint8_t accesses = 0;
        Client::Render::Models::CrtRandom random(1);

        const Client::Scenes::FullSetScan scan = Client::Scenes::ScanFullSet(character, own, accesses, random);

        ASSERT_TRUE(scan.complete);
        const bool withoutAHelm = IsNamed(items->table, ArmorGroup, index) && !IsNamed(items->table, HelmGroup, index);
        EXPECT_EQ(scan.globals.byte743a17c == 1, withoutAHelm);
        kept += scan.globals.byte743a17c;
    }
    EXPECT_EQ(kept, 4);
}
} // namespace
