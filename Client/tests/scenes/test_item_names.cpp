#include "Scenes/ItemNames.h"

#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Scenes/NpcNames.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

// 0x5b1c70, el nombre de un ítem, con el anillo de transformación (0x1a0a): los monstruos de 0x7681eb8 (NpcName.txt).
namespace
{
using Client::Scenes::NpcName;
using Client::Scenes::NpcNameTable;

constexpr std::uint16_t TransformationRing = 0x1a0a;

NpcName Record(std::uint16_t type, std::string_view name)
{
    NpcName record{};
    record.type = type;
    std::ranges::copy(name, record.name.begin());
    return record;
}

class ItemNamesTest : public ::testing::Test
{
protected:
    ItemNamesTest()
    {
        // El 7 dos veces: 0x5b21a6 no corta en el primero.
        table[3] = Record(7, "Gigante A");
        table[4] = Record(2, "Dragon");
        table[10] = Record(7, "Gigante B");
    }

    Client::Core::Expected<std::string, std::string> Name(std::int32_t level)
    {
        return Client::Scenes::ItemNameOf(nullptr, &texts, &table, TransformationRing, level);
    }

    NpcNameTable table = NpcNameTable(Client::Scenes::NpcNameCount);
    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable({{103, "Anillo"}});
};

// 0x5b21a6..0x5b21ef: el monstruo del nivel en 0x6c9338 (2, 7, 14, 8, 9 y 41 del 0 al 5) y, por cada registro de
// 0x7681eb8 con ese tipo, sprintf(buffer, "%s %s", su nombre, el 103): queda el último.
TEST_F(ItemNamesTest, TheTransformationRingTakesTheLastMonsterOfItsLevel)
{
    const auto giant = Name(1);
    ASSERT_TRUE(giant.HasValue()) << giant.Error();
    EXPECT_EQ(giant.Value(), "Gigante B Anillo");
    const auto dragon = Name(0);
    ASSERT_TRUE(dragon.HasValue()) << dragon.Error();
    EXPECT_EQ(dragon.Value(), "Dragon Anillo");
}

// Sin un registro del monstruo (el 14 del nivel 2) no escribe el buffer: vacío, como los demás que ItemNameOf no
// escribe. Del 6 al 10, 0x6c9338 lee los bytes de la tabla que sigue (0x6c9350): del 6 al 9 no entran en 16 bits y el
// del 10 es 0xc7b1, que la tabla no tiene. Del 11 al 15, el 0, el tipo de los registros vacíos de la tabla: queda el
// último, con el nombre vacío.
TEST_F(ItemNamesTest, TheTransformationRingWithoutItsMonsterLeavesTheBuffer)
{
    for (const std::int32_t level : {2, 6, 7, 10})
    {
        const auto name = Name(level);
        ASSERT_TRUE(name.HasValue()) << name.Error();
        EXPECT_EQ(name.Value(), "") << level;
    }
    const auto empty = Name(11);
    ASSERT_TRUE(empty.HasValue()) << empty.Error();
    EXPECT_EQ(empty.Value(), " Anillo");
}

// Sin la tabla de 0x7681eb8, corta.
TEST_F(ItemNamesTest, TheTransformationRingNeedsTheMonsterTable)
{
    const auto name = Client::Scenes::ItemNameOf(nullptr, &texts, nullptr, TransformationRing, 1);
    ASSERT_FALSE(name.HasValue());
    EXPECT_NE(name.Error().find("0x5b21a6"), std::string::npos) << name.Error();
}
} // namespace
