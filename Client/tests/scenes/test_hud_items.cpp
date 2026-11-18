#include "Scenes/HudItems.h"

#include "Data/Tables/ItemTable.h"
#include "Scenes/OwnInventory.h"
#include "World/ItemState.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// Lo que el HUD lee de los ítems (CL8 hito 2): las ranuras rápidas (0x558110 y 0x558490), las flechas del
// inventario (0x557d80), la durabilidad máxima (0x5b1ba0) y el nombre de Item.bmd.
namespace
{
using Client::Scenes::CountQuickSlotItems;
using Client::Scenes::CountTheAmmo;
using Client::Scenes::FindQuickSlotItem;
using Client::Scenes::InventoryCellCount;
using Client::Scenes::ItemRecordName;
using Client::Scenes::MaxDurability;
using Client::Scenes::QuickSlots;
using Client::World::ItemState;

using Cells = std::array<ItemState, InventoryCellCount>;
using Record = std::array<std::byte, Client::Data::Tables::ItemRecordSize>;

// La casilla (fila · 8 + columna) con `type`, `count` en D+0x1a, el nivel en D+4 (bits 3 a 6) y +0x3c en 1.
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

// 0x5582c8..0x558376: los tipos de arriba abajo y, en cada uno, las columnas de la 7 a la 0 y las filas de la 7 a
// la 0. La 0x1c03 gana a la 0x1c00 aunque esté antes en la grilla; en la misma columna, la fila de abajo.
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

    // Una casilla con +0x3c en 0 no cuenta.
    for (ItemState& item : cells)
    {
        if (item.id == 0x1c03)
            item.occupied = 0;
    }
    EXPECT_EQ(Found(slots, cells, 0), 0u);
    cells[0].occupied = 0;
    EXPECT_EQ(Found(slots, cells, 0), std::nullopt);
}

// Los rangos de cada ranura: la de vida y la de maná se cruzan, y la 2 cae en el antídoto con un tipo que no es
// de ningún rango.
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
    // Con un tipo de otro rango, la 0 y la 1 vuelven al suyo.
    slots.types = {0x1234, 0x1234, 0x1c00};
    EXPECT_EQ(Found(slots, cells, 0), 0u);
    EXPECT_EQ(Found(slots, cells, 1), 1u);
    EXPECT_EQ(Found(slots, cells, 2), 0u);
}

// 0x1c07, 0x1c0a y 0x1c14 piden el nivel de la ranura ((D+4 >> 3) & 0xf); con el pedido trabado (0x576794c), el
// 0x1c0a no se busca.
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

// Un 0x1c14 con nivel deja el rango en locales sin inicializar (0x558191 y 0x5584ec): el error, con la dirección.
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

// 0x558119..0x558141 (CL9 hito 4): las skills piden las pociones de maná con 3 (0x1c04 a 0x1c06) y las de vida con 4
// (0x1c00 a 0x1c03), sin mirar las ranuras, su nivel ni 0x576794c; con 5 el rango sale de locales sin inicializar.
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
    // La 0x1c05 gana a la 0x1c04 y la 0x1c02 a la 0x1c00, aunque estén después en la grilla.
    EXPECT_EQ(Found(slots, cells, Client::Scenes::ManaPotionSlot, true), 7u * 8u + 1u);
    EXPECT_EQ(Found(slots, cells, Client::Scenes::LifePotionSlot, true), 1u * 8u + 6u);
    cells[7 * 8 + 1].occupied = 0;
    EXPECT_EQ(Found(slots, cells, Client::Scenes::ManaPotionSlot), 0u);
    const Cells empty{};
    EXPECT_EQ(Found(slots, empty, Client::Scenes::ManaPotionSlot), std::nullopt);
    EXPECT_EQ(Found(slots, empty, Client::Scenes::LifePotionSlot), std::nullopt);
    const auto fifth = FindQuickSlotItem(slots, cells, 5, false);
    ASSERT_FALSE(fifth.HasValue());
    EXPECT_NE(fifth.Error().find("0x558110(5)"), std::string::npos) << fifth.Error();
}

// 0x558490: las pociones suman D+0x1a en todo su rango; 0x1c09, 0x1c0a y 0x1c14 cuentan casillas.
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

// 0x557d80: solo el elfo (clase & 7 en 2); el arco de la izquierda pide flechas (0x80f) y la ballesta de la derecha
// virotes (0x807), y cuenta casillas.
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

// 0x5b1ba0: la durabilidad de Item.bmd (+0x2d, o +0x2e en los báculos) más 1, 2, 3, 4, 5 o 6 por nivel, y 20 por
// ancestral o 15 por excelente.
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

// El "%s" de Item.bmd: hasta el primer '\0', o los 0x54 bytes.
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
