#include "Items/ItemNames.h"

#include "Core/Expected.h"
#include "Tables/ItemTable.h"
#include "Tables/TextTable.h"
#include "Monsters/NpcNames.h"
#include "support/ItemFiles.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// 0x5b1c70, el nombre de un ítem, con el anillo de transformación (0x1a0a): los monstruos de 0x7681eb8 (NpcName.txt).
namespace
{
using Client::Monsters::NpcName;
using Client::Monsters::NpcNameTable;

constexpr std::uint16_t TransformationRing = 0x1a0a;
constexpr std::uint16_t EventTicket = 0x1c0c;
// Item.bmd+0x2d, el byte que 0x5b1d54 lee del registro.
constexpr std::size_t PrizeOffset = 0x2d;

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
        return Client::Items::ItemNameOf(nullptr, &texts, &table, TransformationRing, level);
    }

    NpcNameTable table = NpcNameTable(Client::Monsters::NpcNameCount);
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable({{103, "Anillo"}});
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

// 0x5b1d52..0x5b1d6e: el 0x1c0c de nivel 2 (el boleto), sprintf(buffer, "%s" (0x6c98f0), 0x6c9350 + Item.bmd+0x2d ·
// 100): el premio de los diez de 100 bytes de la .data, en coreano (EUC-KR), con sus bytes tal cual. El 4 es "256M 램".
TEST(ItemNamesTicketTest, TheTicketOfLevelTwoNamesThePrizeOfItsRecord)
{
    const Client::Tables::ItemTable items =
        Client::Tests::ItemTableOf({}, {{.type = EventTicket, .offset = PrizeOffset, .value = 4}});
    const auto name = Client::Items::ItemNameOf(&items, nullptr, nullptr, EventTicket, 2);
    ASSERT_TRUE(name.HasValue()) << name.Error();
    EXPECT_EQ(name.Value(), "256M \xb7\xa5");
}

// 0x6c9350..0x6c9737: los diez premios con los largos de la .data del c32bbd5f (hasta su '\0'), y el 0 entero.
TEST(ItemNamesTicketTest, TheTenPrizesAreTheBytesOfTheData)
{
    constexpr std::array<std::size_t, 10> Lengths{18, 14, 12, 30, 7, 17, 16, 9, 8, 20};
    for (std::uint32_t i = 0; i < Lengths.size(); ++i)
    {
        const std::optional<std::string_view> prize = Client::Items::EventTicketPrizeOf(i);
        ASSERT_TRUE(prize.has_value()) << i;
        EXPECT_EQ(prize->size(), Lengths[i]) << i;
    }
    EXPECT_EQ(Client::Items::EventTicketPrizeOf(0),
              std::string_view("\xc8\xf7\xb5\xf9\xc5\xa9 \xb0\xed\xc7\xe2 \xbf\xa9\xc7\xe0\xb1\xc7"));
    EXPECT_EQ(Client::Items::EventTicketPrizeOf(1), std::string_view("\xc6\xe6\xc6\xbc\xbe\xf6"
                                                                     "4 \xc4\xc4\xc7\xbb\xc5\xcd"));
    EXPECT_FALSE(Client::Items::EventTicketPrizeOf(10).has_value());
}

// Desde el 10, 0x6c9350 + i · 100 lee la .data que sigue a los diez (strings de formato y globales que el juego
// cambia), que MuRet no tiene: corta. Sin Item.bmd, también.
TEST(ItemNamesTicketTest, TheTicketPrizeOutsideTheTenCuts)
{
    const Client::Tables::ItemTable items =
        Client::Tests::ItemTableOf({}, {{.type = EventTicket, .offset = PrizeOffset, .value = 10}});
    const auto outside = Client::Items::ItemNameOf(&items, nullptr, nullptr, EventTicket, 2);
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("0x5b1d60"), std::string::npos) << outside.Error();
    const auto unloaded = Client::Items::ItemNameOf(nullptr, nullptr, nullptr, EventTicket, 2);
    ASSERT_FALSE(unloaded.HasValue());
    EXPECT_NE(unloaded.Error().find("0x5b1c76"), std::string::npos) << unloaded.Error();
}

// Sin la tabla de 0x7681eb8, corta.
TEST_F(ItemNamesTest, TheTransformationRingNeedsTheMonsterTable)
{
    const auto name = Client::Items::ItemNameOf(nullptr, &texts, nullptr, TransformationRing, 1);
    ASSERT_FALSE(name.HasValue());
    EXPECT_NE(name.Error().find("0x5b21a6"), std::string::npos) << name.Error();
}
} // namespace
