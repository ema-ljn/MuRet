#include "Scenes/OwnStatHelpers.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/FullSetScan.h"
#include "Scenes/ItemSetOptionList.h"
#include "Scenes/OwnCharacterRecord.h"
#include "World/ItemState.h"
#include "support/PlayerActors.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Los ocho helpers que 0x550d10 llama en 0x551287..0x5512ba y 0x54ee00 (06, "Los ocho helpers de los
// stats propios"). Los valores salen del ASM cotejado con el PE c32bbd5f y del modelo de la CRT para los
// rand(); el Item.bmd de estos tests es sintético (el real, en test_golden_own_stat_helpers.cpp).
namespace
{
using Client::Data::Tables::ItemTable;
using Client::Render::Models::CrtRandom;
using Client::Render::Models::OwnRecordCloseRandoms;
using Client::Scenes::ItemDurabilityFactor;
using Client::Scenes::ItemSetOptionList;
using Client::Scenes::OwnCharacterRecord;
using Client::Scenes::OwnEquipmentOverride;
using Client::Scenes::Scan505820;
using Client::Tests::AfterDraws;
using Client::World::ItemState;

constexpr std::size_t BaseDurabilityOffset = 0x2d;
constexpr std::uint16_t EmptyType = 0xffff;

constexpr std::size_t RightHand = 0;
constexpr std::size_t LeftHand = 1;
constexpr std::size_t Helm = 2;
constexpr std::size_t Armor = 3;
constexpr std::size_t Pants = 4;
constexpr std::size_t Gloves = 5;
constexpr std::size_t Boots = 6;
constexpr std::size_t Wings = 7;
constexpr std::size_t Pet = 8;
constexpr std::size_t Slot9 = 9;
constexpr std::size_t Slot10 = 10;
constexpr std::size_t Slot11 = 11;

constexpr std::uint8_t DarkWizard = 0;
constexpr std::uint8_t DarkKnight = 1;
constexpr std::uint8_t Elf = 2;
constexpr std::uint8_t MagicGladiator = 3;
constexpr std::uint8_t DarkLord = 4;

// El tipo de Item.bmd: 16 secciones de 512.
constexpr std::uint16_t TypeOf(int group, int index)
{
    return static_cast<std::uint16_t>(group * 512 + index);
}

constexpr std::uint16_t Kris = TypeOf(0, 0);
constexpr std::uint16_t ShortBow = TypeOf(4, 0);
constexpr std::uint16_t Bolt = TypeOf(4, 7);
constexpr std::uint16_t Crossbow = TypeOf(4, 8);
constexpr std::uint16_t Arrow = TypeOf(4, 15);
constexpr std::uint16_t SkullStaff = TypeOf(5, 0);
constexpr std::uint16_t SmallShield = TypeOf(6, 0);
constexpr std::uint16_t BronzeHelm = TypeOf(7, 0);
constexpr std::uint16_t BronzeBoots = TypeOf(11, 0);
constexpr std::uint16_t WingsOfElf = TypeOf(12, 0);
constexpr std::uint16_t WizardsRing = TypeOf(13, 20);
constexpr std::uint16_t CapeOfLord = TypeOf(13, 30);

struct BaseDurability
{
    std::uint16_t type = 0;
    std::uint8_t value = 0;
};

// Item.bmd sintético: el byte +0x2d de cada tipo de `bases` y todo lo demás en 0.
ItemTable ItemsWithBaseDurability(std::initializer_list<BaseDurability> bases)
{
    std::vector<std::byte> plain(Client::Data::Tables::ItemTableSize);
    for (const BaseDurability& base : bases)
    {
        plain[base.type * Client::Data::Tables::ItemRecordSize + BaseDurabilityOffset] = std::byte{base.value};
    }
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Data::Tables::ItemRecordSize);
    return ItemTable(std::span<const std::byte>(encrypted));
}

struct Option
{
    std::uint8_t tag = 0;
    std::uint8_t value = 0;
};

// Un ítem del tipo y la durabilidad dados, con sus opciones (D+0x2b el tag y D+0x33 el valor).
ItemState Item(std::uint16_t type, std::uint8_t durability, std::initializer_list<Option> options = {})
{
    ItemState item;
    item.id = type;
    item.durability = durability;
    std::size_t index = 0;
    for (const Option& option : options)
    {
        item.optionBytes[index] = option.tag;
        item.optionBytes[Client::World::ItemOptionValueOffset + index] = option.value;
        ++index;
    }
    item.optionCount = static_cast<std::uint8_t>(index);
    return item;
}

// Un registro de la clase dada con los doce lugares vacíos (el tipo −1).
OwnCharacterRecord EmptyRecord(std::uint8_t characterClass)
{
    OwnCharacterRecord own;
    own.characterClass = characterClass;
    for (ItemState& item : own.equipment)
    {
        item.id = EmptyType;
    }
    return own;
}

struct SetEntry
{
    std::uint8_t kind = 0;
    std::uint8_t flag = 0;
    std::int32_t value = 0;
};

// La lista de [0x6eed3c] con las entradas en +0x9348 y las marcas en 0.
ItemSetOptionList SetOptionsOf(std::initializer_list<SetEntry> entries)
{
    ItemSetOptionList list;
    std::size_t index = 0;
    for (const SetEntry& entry : entries)
    {
        list.entries[index] = {entry.kind, entry.flag, std::uint8_t{0}};
        list.values[index] = entry.value;
        ++index;
    }
    list.count9348 = static_cast<std::uint8_t>(index);
    return list;
}

// ---- 0x54ee00 ----

// r = 1 − durabilidad / máximo, en doble, contra 0.8f, 0.7f y 0.5f. 40 de 200 deja 0.8 en doble, que no
// pasa 0.8f; 6 de 20 deja 0.7 en doble, que pasa 0.7f (con PC24 el cociente sería 0.3f y no pasaría).
TEST(OwnStatHelpersTest, TheDurabilityFactorFollowsTheShareOfDurabilityLeft)
{
    EXPECT_EQ(ItemDurabilityFactor(2, 22, 0, 0, 0), 0.5f);
    EXPECT_EQ(ItemDurabilityFactor(30, 200, 0, 0, 0), 0.5f);
    EXPECT_EQ(ItemDurabilityFactor(40, 200, 0, 0, 0), 0.3f);
    EXPECT_EQ(ItemDurabilityFactor(6, 20, 0, 0, 0), 0.3f);
    EXPECT_EQ(ItemDurabilityFactor(6, 15, 0, 0, 0), 0.2f);
    EXPECT_EQ(ItemDurabilityFactor(8, 16, 0, 0, 0), 0.0f);
    EXPECT_EQ(ItemDurabilityFactor(16, 16, 0, 0, 0), 0.0f);
    EXPECT_EQ(ItemDurabilityFactor(200, 16, 0, 0, 0), 0.0f);
}

// El máximo suma 1 por cada uno de los primeros 4 niveles y 2 por los otros; 20 con D+0x1c % 4 en 1 o 2
// o, si no, 15 con D+0x1b & 0x3f. Con el máximo en 0 ninguna comparación pasa.
TEST(OwnStatHelpersTest, TheMaximumDurabilityGrowsWithTheLevelAndTheFlags)
{
    EXPECT_EQ(ItemDurabilityFactor(6, 14, 0, 0, 0), 0.2f);
    EXPECT_EQ(ItemDurabilityFactor(6, 14, 5 << 3, 0, 0), 0.3f); // 14 + 4 + 2 = 20
    EXPECT_EQ(ItemDurabilityFactor(6, 16, 4 << 3, 0, 0), 0.3f); // 16 + 4 = 20
    EXPECT_EQ(ItemDurabilityFactor(6, 0, 0x1ff, 0, 0), 0.3f);   // nivel 15: 4 + 22 = 26

    EXPECT_EQ(ItemDurabilityFactor(6, 0, 0, 0, 1), 0.3f);
    EXPECT_EQ(ItemDurabilityFactor(6, 0, 0, 0, 0xfe), 0.3f);
    EXPECT_EQ(ItemDurabilityFactor(6, 0, 0, 0, 3), 0.0f);
    EXPECT_EQ(ItemDurabilityFactor(6, 0, 0, 0x01, 0), 0.2f); // 15
    EXPECT_EQ(ItemDurabilityFactor(6, 0, 0, 0x40, 0), 0.0f);
    EXPECT_EQ(ItemDurabilityFactor(6, 0, 0, 0x01, 1), 0.3f); // 20 y no 35, que daría 0.5f

    EXPECT_EQ(ItemDurabilityFactor(0, 0, 0, 0, 0), 0.0f);
    EXPECT_EQ(ItemDurabilityFactor(5, 0, 0, 0, 0), 0.0f);
}

// ---- 0x54efe0 ----

// S = +0x2a + fuerza, A = +0x2c + agilidad y E = +0x30 + energía; +0x54 y +0x56 copian +0x50 y +0x52.
TEST(OwnStatHelpersTest, Words50To56StartFromTheClass)
{
    struct Case
    {
        std::uint8_t characterClass;
        std::uint16_t word50;
        std::uint16_t word52;
    };
    for (const Case& expected :
         {Case{DarkWizard, 15, 30}, Case{DarkKnight, 20, 30}, Case{Elf, 29, 51}, Case{MagicGladiator, 28, 42},
          Case{DarkLord, 23, 33}, Case{5, 15, 30}, Case{6, 15, 30}, Case{7, 15, 30}, Case{0x0c, 23, 33}})
    {
        SCOPED_TRACE(expected.characterClass);
        OwnCharacterRecord own = EmptyRecord(expected.characterClass);
        own.word2a = 20;
        own.strength = 100; // 120
        own.agility = 84;
        own.energy = 90;
        own.word30 = 6; // 96
        ItemSetOptionList setOptions;

        ASSERT_TRUE(Client::Scenes::UpdateOwnWords50To56(own, {}, nullptr, &setOptions).HasValue());

        EXPECT_EQ(own.word50, expected.word50);
        EXPECT_EQ(own.word52, expected.word52);
        EXPECT_EQ(own.word54, expected.word50);
        EXPECT_EQ(own.word56, expected.word52);
    }
}

// Ballesta en la derecha o arco en la izquierda, con durabilidad: S/14 + A/7 y A/4 + S/8, en cualquier
// clase. Bolt de nivel 2 en la izquierda: w + _ftol(w · 5 · 0.01f + 1), que con el float da 168 y 294
// (con 0.05 exacto serían 169 y 295), y no toca +0x54 ni +0x56. Arrow de nivel 1 en la derecha, esos dos.
TEST(OwnStatHelpersTest, TheArcherBranchAndTheAmmunition)
{
    const ItemTable items = ItemsWithBaseDurability({});
    const auto archer = [](ItemState right, ItemState left)
    {
        OwnCharacterRecord own = EmptyRecord(Elf);
        own.strength = 280;
        own.agility = 980;
        own.equipment[RightHand] = right;
        own.equipment[LeftHand] = left;
        return own;
    };
    ItemSetOptionList setOptions;

    ItemState bolt = Item(Bolt, 1);
    bolt.scalar1Low = 2 << 3;
    OwnCharacterRecord own = archer(Item(Crossbow, 1), bolt);
    ASSERT_TRUE(Client::Scenes::UpdateOwnWords50To56(own, {}, &items, &setOptions).HasValue());
    EXPECT_EQ(own.word50, 168);
    EXPECT_EQ(own.word52, 294);
    EXPECT_EQ(own.word54, 160);
    EXPECT_EQ(own.word56, 280);

    ItemState arrow = Item(Arrow, 1);
    arrow.scalar1Low = 1 << 3;
    own = archer(arrow, Item(ShortBow, 1));
    ASSERT_TRUE(Client::Scenes::UpdateOwnWords50To56(own, {}, &items, &setOptions).HasValue());
    EXPECT_EQ(own.word50, 160);
    EXPECT_EQ(own.word52, 280);
    EXPECT_EQ(own.word54, 165);
    EXPECT_EQ(own.word56, 289);

    // Sin nivel, nada; Bolt sola no es la rama, ni la ballesta sin durabilidad: la del Elf.
    own = archer(Item(Crossbow, 1), Item(Bolt, 1));
    ASSERT_TRUE(Client::Scenes::UpdateOwnWords50To56(own, {}, &items, &setOptions).HasValue());
    EXPECT_EQ(own.word50, 160);
    EXPECT_EQ(own.word52, 280);
    own = archer(Item(EmptyType, 0), bolt);
    ASSERT_TRUE(Client::Scenes::UpdateOwnWords50To56(own, {}, &items, &setOptions).HasValue());
    EXPECT_EQ(own.word50, 180);
    EXPECT_EQ(own.word52, 315);
    own = archer(Item(Crossbow, 0), Item(EmptyType, 0));
    ASSERT_TRUE(Client::Scenes::UpdateOwnWords50To56(own, {}, &items, &setOptions).HasValue());
    EXPECT_EQ(own.word50, 180);
    EXPECT_EQ(own.word52, 315);
}

// Un DK con S = 120 (20 y 30). Las alas (+0x2d en 200, 30 de durabilidad: 0.5f) suman sus 0x50 (9 − 4 =
// 5) a los cuatro. La derecha (Kris, 2 de 22: 0.5f) suma D+0xc y D+0xe más sus 0x50 reducidos (11 y 17)
// a +0x50 y +0x52, y 0x5d (4) y 0x5e por 2 a esos dos. El escudo (factor 0) suma 10 y 6 a +0x54 y +0x56
// y su 0x5d. El lugar 9 suma su 0x5d a los cuatro y el 10, 0x5e por 10; el 11, sin durabilidad, no.
TEST(OwnStatHelpersTest, Words50To56AddTheWingsTheHandsAndTheAccessories)
{
    const ItemTable items = ItemsWithBaseDurability({{Kris, 22}, {WingsOfElf, 200}});
    OwnCharacterRecord own = EmptyRecord(DarkKnight);
    own.strength = 120;
    own.equipment[Wings] = Item(WingsOfElf, 30, {{0x50, 9}});
    own.equipment[RightHand] = Item(Kris, 2, {{0x50, 2}, {0x5d, 4}, {0x5e, 0}});
    own.equipment[RightHand].word0c = 20;
    own.equipment[RightHand].word0e = 31;
    own.equipment[LeftHand] = Item(SmallShield, 5, {{0x5d, 1}});
    own.equipment[LeftHand].word0c = 10;
    own.equipment[LeftHand].word0e = 6;
    own.equipment[Slot9] = Item(TypeOf(13, 12), 1, {{0x5d, 3}});
    own.equipment[Slot10] = Item(TypeOf(13, 8), 1, {{0x5e, 0}});
    own.equipment[Slot11] = Item(TypeOf(13, 9), 0, {{0x5e, 0}});
    ItemSetOptionList setOptions;

    ASSERT_TRUE(Client::Scenes::UpdateOwnWords50To56(own, {}, &items, &setOptions).HasValue());

    // 25 + 11 + 4 = 40 (más 0), + 3 = 43, + 4 = 47; 35 + 17 + 4 = 56, + 1 = 57, + 3 = 60, + 6 = 66;
    // 25 + 10 + 1 + 3 = 39, + 3 = 42; 35 + 6 + 1 + 3 = 45, + 4 = 49.
    EXPECT_EQ(own.word50, 47);
    EXPECT_EQ(own.word52, 66);
    EXPECT_EQ(own.word54, 42);
    EXPECT_EQ(own.word56, 49);
}

// Una derecha de 0xa00 a 0xc00 (con signo, los dos incluidos) va a +0x54 y +0x56; las alas con el +0x2d en
// 0 no suman.
TEST(OwnStatHelpersTest, AStaffInTheRightHandGoesToWords54And56)
{
    const ItemTable items = ItemsWithBaseDurability({});
    ItemSetOptionList setOptions;
    struct Case
    {
        std::uint16_t type;
        bool secondPair;
    };
    for (const Case& right :
         {Case{SkullStaff, true}, Case{SmallShield, true}, Case{Kris, false}, Case{TypeOf(6, 1), false}})
    {
        SCOPED_TRACE(right.type);
        OwnCharacterRecord own = EmptyRecord(DarkWizard);
        own.equipment[RightHand] = Item(right.type, 1);
        own.equipment[RightHand].word0c = 7;
        own.equipment[RightHand].word0e = 9;
        own.equipment[Wings] = Item(TypeOf(12, 1), 1, {{0x50, 9}});

        ASSERT_TRUE(Client::Scenes::UpdateOwnWords50To56(own, {}, &items, &setOptions).HasValue());

        EXPECT_EQ(own.word50, right.secondPair ? 0 : 7);
        EXPECT_EQ(own.word52, right.secondPair ? 0 : 9);
        EXPECT_EQ(own.word54, right.secondPair ? 7 : 0);
        EXPECT_EQ(own.word56, right.secondPair ? 9 : 0);
    }
}

// 0x91 a +0x54 y +0x50, 0x92 a +0x56 y +0x52, y A · 50 / 100 (0xa5) y S · 10 / 100 (0xa4) a los cuatro.
TEST(OwnStatHelpersTest, Words50To56AddTheSetOptions)
{
    OwnCharacterRecord own = EmptyRecord(DarkWizard);
    own.strength = 120;
    own.agility = 84;
    ItemSetOptionList setOptions = SetOptionsOf({{0x05, 0, 7}, {0x06, 0, 11}, {0x19, 1, 50}, {0x18, 1, 10}});

    ASSERT_TRUE(Client::Scenes::UpdateOwnWords50To56(own, {}, nullptr, &setOptions).HasValue());

    EXPECT_EQ(own.word50, 15 + 7 + 42 + 12);
    EXPECT_EQ(own.word52, 30 + 11 + 42 + 12);
    EXPECT_EQ(own.word54, 15 + 7 + 42 + 12);
    EXPECT_EQ(own.word56, 30 + 11 + 42 + 12);
    EXPECT_EQ(setOptions.used, (std::array<std::uint8_t, 16>{1, 1, 1, 1}));
}

// El ítem de 0x7879f80 reemplaza la mano derecha con [0x7879fbc] > 0, la ventana y el lugar 0.
TEST(OwnStatHelpersTest, Words50To56ReadTheHeldItemInsteadOfASlot)
{
    const ItemTable items = ItemsWithBaseDurability({});
    ItemSetOptionList setOptions;
    OwnCharacterRecord own = EmptyRecord(DarkWizard);
    own.equipment[RightHand] = Item(Kris, 1);
    own.equipment[RightHand].word0c = 5;
    OwnEquipmentOverride held;
    held.dword7879fbc = 1;
    held.window78ab9b8Is78a9bb0 = true;
    held.slot78a6758 = 0;
    held.item7879f80 = Item(SkullStaff, 1);
    held.item7879f80.word0c = 7;

    ASSERT_TRUE(Client::Scenes::UpdateOwnWords50To56(own, held, &items, &setOptions).HasValue());
    EXPECT_EQ(own.word50, 0);
    EXPECT_EQ(own.word54, 7);

    for (const std::int32_t count : {0, -1})
    {
        held.dword7879fbc = count;
        ASSERT_TRUE(Client::Scenes::UpdateOwnWords50To56(own, held, &items, &setOptions).HasValue());
        EXPECT_EQ(own.word50, 5);
        EXPECT_EQ(own.word54, 0);
    }
    held.dword7879fbc = 1;
    held.window78ab9b8Is78a9bb0 = false;
    ASSERT_TRUE(Client::Scenes::UpdateOwnWords50To56(own, held, &items, &setOptions).HasValue());
    EXPECT_EQ(own.word50, 5);
}

// Lo que falta sale como error donde el original lo lee, con lo escrito hasta ahí.
TEST(OwnStatHelpersTest, Words50To56StopWhereTheyReadWhatMuRetLacks)
{
    OwnCharacterRecord own = EmptyRecord(DarkKnight);
    own.strength = 120;
    own.equipment[RightHand] = Item(Kris, 1);
    ItemSetOptionList setOptions;
    auto updated = Client::Scenes::UpdateOwnWords50To56(own, {}, nullptr, &setOptions);
    ASSERT_FALSE(updated.HasValue());
    EXPECT_NE(updated.Error().find("Item.bmd"), std::string::npos);
    EXPECT_EQ(own.word50, 20);

    const ItemTable items = ItemsWithBaseDurability({});
    updated = Client::Scenes::UpdateOwnWords50To56(own, {}, &items, nullptr);
    ASSERT_FALSE(updated.HasValue());
    EXPECT_NE(updated.Error().find("0x40e1b0"), std::string::npos);

    own.equipment[RightHand].optionCount = 10;
    updated = Client::Scenes::UpdateOwnWords50To56(own, {}, &items, &setOptions);
    ASSERT_FALSE(updated.HasValue());
    EXPECT_NE(updated.Error().find("10 opciones"), std::string::npos);
}

// ---- 0x54f9b0 ----

// E/9 y E/4 (E = +0x30 + energía). 0x40d690 suma a +0xef4 sin ponerlo en 0, y +0xef4 queda con lo de
// 0x40d7f0: la llamada siguiente lo vuelve a sumar.
TEST(OwnStatHelpersTest, Words5aAnd5cCarryWordEf4IntoTheNextCall)
{
    OwnCharacterRecord own = EmptyRecord(DarkWizard);
    own.energy = 80;
    own.word30 = 10;
    own.wordef4 = 5;
    ItemSetOptionList setOptions = SetOptionsOf({{0x07, 0, 3}, {0x1c, 1, 10}});

    ASSERT_TRUE(Client::Scenes::UpdateOwnWords5aAnd5c(own, {}, nullptr, &setOptions).HasValue());
    EXPECT_EQ(own.word5a, 10 + 8 + 9);
    EXPECT_EQ(own.word5c, 22 + 8 + 9);
    EXPECT_EQ(own.wordef4, 9);

    // Las entradas ya están marcadas: suma el 9 que quedó.
    ASSERT_TRUE(Client::Scenes::UpdateOwnWords5aAnd5c(own, {}, nullptr, &setOptions).HasValue());
    EXPECT_EQ(own.word5a, 10 + 9);
    EXPECT_EQ(own.word5c, 22 + 9);
    EXPECT_EQ(own.wordef4, 0);
}

// La derecha suma sus 0x51 reducidas (0x50 con 0 21, 0 23 y 0 31), 0x5f y 0x60 por 2. Con E = 900: 100 y
// 225; 9 − 4 = 5 o 7 − 3 = 4; 6; y w · 2 / 100.
TEST(OwnStatHelpersTest, Words5aAnd5cAddTheRightHand)
{
    const ItemTable items =
        ItemsWithBaseDurability({{TypeOf(0, 21), 22}, {TypeOf(0, 23), 22}, {TypeOf(0, 31), 22}, {TypeOf(0, 1), 22}});
    struct Case
    {
        std::uint16_t type;
        std::uint16_t word5a;
        std::uint16_t word5c;
    };
    for (const Case& expected : {Case{TypeOf(0, 21), 113, 240}, Case{TypeOf(0, 23), 113, 240},
                                 Case{TypeOf(0, 31), 113, 240}, Case{TypeOf(0, 1), 112, 239}})
    {
        SCOPED_TRACE(expected.type);
        OwnCharacterRecord own = EmptyRecord(DarkWizard);
        own.energy = 900;
        own.equipment[RightHand] = Item(expected.type, 2, {{0x50, 9}, {0x51, 7}, {0x5f, 6}, {0x60, 0}});
        ItemSetOptionList setOptions;

        ASSERT_TRUE(Client::Scenes::UpdateOwnWords5aAnd5c(own, {}, &items, &setOptions).HasValue());

        EXPECT_EQ(own.word5a, expected.word5a);
        EXPECT_EQ(own.word5c, expected.word5c);
    }
}

// Las alas suman sus 0x51 reducidas también con el +0x2d en 0 (factor 0). El lugar 9: 0x5f y 0x60 por 2;
// el 10: 0x60 por 10.
TEST(OwnStatHelpersTest, Words5aAnd5cAddTheWingsAndTheAccessories)
{
    const ItemTable items = ItemsWithBaseDurability({{WingsOfElf, 200}});
    ItemSetOptionList setOptions;
    OwnCharacterRecord own = EmptyRecord(DarkWizard);
    own.equipment[Wings] = Item(WingsOfElf, 30, {{0x51, 9}});
    ASSERT_TRUE(Client::Scenes::UpdateOwnWords5aAnd5c(own, {}, &items, &setOptions).HasValue());
    EXPECT_EQ(own.word5a, 5);
    EXPECT_EQ(own.word5c, 5);

    own.equipment[Wings] = Item(TypeOf(12, 1), 30, {{0x51, 9}});
    ASSERT_TRUE(Client::Scenes::UpdateOwnWords5aAnd5c(own, {}, &items, &setOptions).HasValue());
    EXPECT_EQ(own.word5a, 9);
    EXPECT_EQ(own.word5c, 9);

    own = EmptyRecord(DarkWizard);
    own.energy = 900;
    own.equipment[Slot9] = Item(TypeOf(13, 12), 1, {{0x5f, 4}, {0x60, 0}});
    own.equipment[Slot10] = Item(TypeOf(13, 8), 1, {{0x60, 0}});
    ASSERT_TRUE(Client::Scenes::UpdateOwnWords5aAnd5c(own, {}, nullptr, &setOptions).HasValue());
    EXPECT_EQ(own.word5a, 116); // 104, 106, 116
    EXPECT_EQ(own.word5c, 256); // 229, 233, 256
}

TEST(OwnStatHelpersTest, Words5aAnd5cStopWhereTheyReadWhatMuRetLacks)
{
    OwnCharacterRecord own = EmptyRecord(DarkWizard);
    own.energy = 90;
    auto updated = Client::Scenes::UpdateOwnWords5aAnd5c(own, {}, nullptr, nullptr);
    ASSERT_FALSE(updated.HasValue());
    EXPECT_NE(updated.Error().find("0x40e1b0"), std::string::npos);
    EXPECT_EQ(own.word5a, 10);
    EXPECT_EQ(own.word5c, 22);

    own.equipment[Wings] = Item(WingsOfElf, 0);
    ItemSetOptionList setOptions;
    updated = Client::Scenes::UpdateOwnWords5aAnd5c(own, {}, nullptr, &setOptions);
    ASSERT_FALSE(updated.HasValue());
    EXPECT_NE(updated.Error().find("Item.bmd"), std::string::npos);
}

// ---- 0x54fd40 ----

// A · 3 / 2 + nivel · 5 + S / 4, y el DL S/6 + L/10 + A · 5 / 2 + nivel · 5 (L = +0x36 + mando); después
// 0x40d690 con 0x95. El nivel por 5 es de 16 bits.
TEST(OwnStatHelpersTest, Word4eByClassAndLevelThenTheSetOptions)
{
    struct Case
    {
        std::uint8_t characterClass;
        std::uint16_t word4e;
    };
    for (const Case& expected : {Case{DarkWizard, 87}, Case{DarkKnight, 87}, Case{Elf, 87}, Case{MagicGladiator, 87},
                                 Case{DarkLord, 113}, Case{5, 87}, Case{0x0c, 113}})
    {
        SCOPED_TRACE(expected.characterClass);
        OwnCharacterRecord own = EmptyRecord(expected.characterClass);
        own.word2a = 5;
        own.strength = expected.word4e == 113 ? 55 : 25; // 60 o 30
        own.word2c = 5;
        own.agility = 15; // 20
        own.word36 = 5;
        own.leadership = 25; // 30
        own.level = 10;
        ItemSetOptionList setOptions = SetOptionsOf({{0x09, 0, 4}});

        ASSERT_TRUE(Client::Scenes::UpdateOwnWord4e(own, &setOptions).HasValue());

        EXPECT_EQ(own.word4e, expected.word4e + 4);
    }

    OwnCharacterRecord own = EmptyRecord(DarkWizard);
    own.level = 13108; // · 5 = 65540
    ItemSetOptionList setOptions;
    ASSERT_TRUE(Client::Scenes::UpdateOwnWord4e(own, &setOptions).HasValue());
    EXPECT_EQ(own.word4e, 4);

    own.level = 10;
    const auto updated = Client::Scenes::UpdateOwnWord4e(own, nullptr);
    ASSERT_FALSE(updated.HasValue());
    EXPECT_NE(updated.Error().find("0x40e1b0"), std::string::npos);
    EXPECT_EQ(own.word4e, 50);
}

// ---- 0x54fe10 ----

TEST(OwnStatHelpersTest, TheSpeedsStartFromTheClassAndTheAgility)
{
    struct Case
    {
        std::uint8_t characterClass;
        std::uint16_t attack;
        std::uint16_t magic;
    };
    for (const Case& expected :
         {Case{DarkWizard, 15, 30}, Case{DarkKnight, 20, 15}, Case{Elf, 6, 6}, Case{MagicGladiator, 20, 15},
          Case{DarkLord, 30, 30}, Case{5, 15, 30}, Case{6, 15, 30}, Case{7, 15, 30}})
    {
        SCOPED_TRACE(expected.characterClass);
        OwnCharacterRecord own = EmptyRecord(expected.characterClass);
        own.word2c = 10;
        own.agility = 290;
        std::uint8_t accesses = 0;
        CrtRandom random(7);

        ASSERT_TRUE(Client::Scenes::UpdateOwnAttackAndMagicSpeeds(own, {}, accesses, random).HasValue());

        EXPECT_EQ(own.attackSpeed, expected.attack);
        EXPECT_EQ(own.magicSpeed, expected.magic);
        EXPECT_EQ(random.State(), CrtRandom(7).State());
    }
}

// El D+0x17 de cada mano (0 sin durabilidad) con el tipo de 0 a 0xbff salvo Bolt y Arrow; con las dos, el
// promedio. Una derecha sin durabilidad cuenta como 0 en el promedio.
TEST(OwnStatHelpersTest, TheSpeedsAddTheWeapons)
{
    struct Case
    {
        ItemState right;
        ItemState left;
        std::uint16_t added;
    };
    const auto withSpeed = [](std::uint16_t type, std::uint8_t durability, std::uint8_t speed)
    {
        ItemState item = Item(type, durability);
        item.byte17 = speed;
        return item;
    };
    const ItemState none = Item(EmptyType, 0);
    for (const Case& expected :
         {Case{withSpeed(Kris, 1, 20), none, 20}, Case{withSpeed(Kris, 1, 20), withSpeed(TypeOf(0, 1), 1, 31), 25},
          Case{withSpeed(Kris, 0, 20), withSpeed(TypeOf(0, 1), 1, 31), 15},
          Case{withSpeed(Bolt, 1, 9), withSpeed(ShortBow, 1, 30), 30},
          Case{withSpeed(ShortBow, 1, 30), withSpeed(Bolt, 1, 9), 30}, Case{withSpeed(Arrow, 1, 9), none, 0},
          Case{withSpeed(SmallShield, 1, 50), none, 0}, Case{none, withSpeed(SmallShield, 1, 50), 0}})
    {
        OwnCharacterRecord own = EmptyRecord(DarkWizard);
        own.equipment[RightHand] = expected.right;
        own.equipment[LeftHand] = expected.left;
        std::uint8_t accesses = 0;
        CrtRandom random;

        ASSERT_TRUE(Client::Scenes::UpdateOwnAttackAndMagicSpeeds(own, {}, accesses, random).HasValue());

        EXPECT_EQ(own.attackSpeed, expected.added);
        EXPECT_EQ(own.magicSpeed, expected.added);
    }
}

// Wizard's Ring con durabilidad y D+4 >> 3 (con signo, sin la máscara del nivel) en 0 o 3: 10 más y el bit
// 0x04 de +0x3c prendido, entre una apertura y un cierre del registro (3.848 rand() si lo deja en 0).
TEST(OwnStatHelpersTest, TheWizardsRingAddsTenAndSetsBit04InsideAnAccess)
{
    constexpr std::uint32_t Seed = 11;
    struct Case
    {
        std::size_t slot;
        std::uint32_t scalar1;
        std::uint8_t durability;
        bool counts;
    };
    for (const Case& ring : {Case{Slot10, 0, 1, true}, Case{Slot11, 3 << 3, 1, true}, Case{Slot10, 7, 1, true},
                             Case{Slot10, 1 << 3, 1, false}, Case{Slot11, 0x80, 1, false}, Case{Slot10, 0, 0, false}})
    {
        SCOPED_TRACE(ring.scalar1);
        OwnCharacterRecord own = EmptyRecord(DarkWizard);
        own.equipment[ring.slot] = Item(WizardsRing, ring.durability);
        own.equipment[ring.slot].scalar1Low = ring.scalar1;
        std::uint8_t accesses = 0;
        CrtRandom random(Seed);

        ASSERT_TRUE(Client::Scenes::UpdateOwnAttackAndMagicSpeeds(own, {}, accesses, random).HasValue());

        EXPECT_EQ(own.attackSpeed, ring.counts ? 10 : 0);
        EXPECT_EQ(own.magicSpeed, ring.counts ? 10 : 0);
        EXPECT_EQ(own.speedAdjustBits, ring.counts ? 0x04 : 0);
        EXPECT_EQ(random.State(), AfterDraws(Seed, ring.counts ? OwnRecordCloseRandoms : 0).State());
        EXPECT_EQ(accesses, 0);
    }

    // Con otro acceso abierto el cierre no lo deja en 0 y no saca nada; los dos anillos suman una vez.
    OwnCharacterRecord own = EmptyRecord(DarkWizard);
    own.equipment[Slot10] = Item(WizardsRing, 1);
    own.equipment[Slot11] = Item(WizardsRing, 1);
    std::uint8_t accesses = 1;
    CrtRandom random(Seed);
    ASSERT_TRUE(Client::Scenes::UpdateOwnAttackAndMagicSpeeds(own, {}, accesses, random).HasValue());
    EXPECT_EQ(own.attackSpeed, 10);
    EXPECT_EQ(accesses, 1);
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());
}

// Sin anillo y con el bit 0x04 prendido lo apaga, en otro acceso; con el bit apagado no abre nada.
TEST(OwnStatHelpersTest, WithoutTheRingBit04IsClearedInsideAnAccess)
{
    constexpr std::uint32_t Seed = 12;
    OwnCharacterRecord own = EmptyRecord(DarkWizard);
    own.speedAdjustBits = 0x06;
    std::uint8_t accesses = 0;
    CrtRandom random(Seed);

    ASSERT_TRUE(Client::Scenes::UpdateOwnAttackAndMagicSpeeds(own, {}, accesses, random).HasValue());
    EXPECT_EQ(own.speedAdjustBits, 0x02);
    EXPECT_EQ(random.State(), AfterDraws(Seed, OwnRecordCloseRandoms).State());

    random = CrtRandom(Seed);
    ASSERT_TRUE(Client::Scenes::UpdateOwnAttackAndMagicSpeeds(own, {}, accesses, random).HasValue());
    EXPECT_EQ(own.speedAdjustBits, 0x02);
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());
}

// Un DK con A = 300 (20 y 15): la Kris (20), el anillo (10), los guantes (7), el bit 0x08 (20) y las 0x61
// de los lugares 0 (5) y 8 (3); el 9, sin durabilidad, no suma.
TEST(OwnStatHelpersTest, TheSpeedsAddTheGlovesTheBitsAndTheOptions61)
{
    OwnCharacterRecord own = EmptyRecord(DarkKnight);
    own.agility = 300;
    own.speedAdjustBits = 0x08;
    own.equipment[RightHand] = Item(Kris, 1, {{0x61, 5}});
    own.equipment[RightHand].byte17 = 20;
    own.equipment[Slot11] = Item(WizardsRing, 1);
    own.equipment[Gloves] = Item(TypeOf(10, 0), 1);
    own.equipment[Gloves].byte17 = 7;
    own.equipment[Pet] = Item(TypeOf(13, 0), 1, {{0x61, 3}});
    own.equipment[Slot9] = Item(TypeOf(13, 12), 0, {{0x61, 50}});
    std::uint8_t accesses = 1;
    CrtRandom random;

    ASSERT_TRUE(Client::Scenes::UpdateOwnAttackAndMagicSpeeds(own, {}, accesses, random).HasValue());

    EXPECT_EQ(own.attackSpeed, 20 + 20 + 10 + 7 + 20 + 5 + 3);
    EXPECT_EQ(own.magicSpeed, 15 + 20 + 10 + 7 + 20 + 5 + 3);
    EXPECT_EQ(own.speedAdjustBits, 0x0c);

    // Los guantes sin durabilidad o con el tipo −1, y el bit 0x01.
    own.speedAdjustBits = 0x01;
    own.equipment[Gloves].durability = 0;
    ASSERT_TRUE(Client::Scenes::UpdateOwnAttackAndMagicSpeeds(own, {}, accesses, random).HasValue());
    EXPECT_EQ(own.attackSpeed, 20 + 20 + 10 + 20 + 5 + 3);
    own.equipment[Gloves] = Item(EmptyType, 1);
    own.equipment[Gloves].byte17 = 7;
    ASSERT_TRUE(Client::Scenes::UpdateOwnAttackAndMagicSpeeds(own, {}, accesses, random).HasValue());
    EXPECT_EQ(own.attackSpeed, 20 + 20 + 10 + 20 + 5 + 3);
}

// El ítem de 0x7879f80 reemplaza las manos (y los guantes y los anillos) pero las 0x61 son del registro.
TEST(OwnStatHelpersTest, TheSpeedsReadTheHeldItemButTheOptionsOfTheRecord)
{
    OwnCharacterRecord own = EmptyRecord(DarkWizard);
    own.agility = 300;
    own.equipment[RightHand] = Item(Kris, 1, {{0x61, 5}});
    own.equipment[RightHand].byte17 = 20;
    OwnEquipmentOverride held;
    held.dword7879fbc = 1;
    held.window78ab9b8Is78a9bb0 = true;
    held.slot78a6758 = 0;
    held.item7879f80 = Item(TypeOf(0, 1), 1, {{0x61, 9}});
    held.item7879f80.byte17 = 40;
    std::uint8_t accesses = 0;
    CrtRandom random;

    ASSERT_TRUE(Client::Scenes::UpdateOwnAttackAndMagicSpeeds(own, held, accesses, random).HasValue());
    EXPECT_EQ(own.attackSpeed, 15 + 40 + 5);
    EXPECT_EQ(own.magicSpeed, 30 + 40 + 5);

    held.slot78a6758 = 1;
    ASSERT_TRUE(Client::Scenes::UpdateOwnAttackAndMagicSpeeds(own, held, accesses, random).HasValue());
    EXPECT_EQ(own.attackSpeed, 15 + 30 + 5);

    held.slot78a6758 = 5;
    held.item7879f80.byte17 = 7;
    ASSERT_TRUE(Client::Scenes::UpdateOwnAttackAndMagicSpeeds(own, held, accesses, random).HasValue());
    EXPECT_EQ(own.attackSpeed, 15 + 20 + 7 + 5);

    held.slot78a6758 = 11;
    held.item7879f80 = Item(WizardsRing, 1);
    ASSERT_TRUE(Client::Scenes::UpdateOwnAttackAndMagicSpeeds(own, held, accesses, random).HasValue());
    EXPECT_EQ(own.attackSpeed, 15 + 20 + 10 + 5);
    EXPECT_EQ(own.speedAdjustBits, 0x04);

    own.equipment[Pet] = Item(TypeOf(13, 0), 1);
    own.equipment[Pet].optionCount = 10;
    const auto updated = Client::Scenes::UpdateOwnAttackAndMagicSpeeds(own, {}, accesses, random);
    ASSERT_FALSE(updated.HasValue());
    EXPECT_NE(updated.Error().find("0x54fe10"), std::string::npos);
}

// ---- 0x5507e0 ----

TEST(OwnStatHelpersTest, Word60StartsFromTheClassAndTheAgility)
{
    struct Case
    {
        std::uint8_t characterClass;
        std::uint16_t word60;
    };
    for (const Case& expected : {Case{DarkWizard, 100}, Case{DarkKnight, 100}, Case{Elf, 75}, Case{MagicGladiator, 100},
                                 Case{DarkLord, 42}, Case{5, 100}, Case{7, 100}})
    {
        SCOPED_TRACE(expected.characterClass);
        OwnCharacterRecord own = EmptyRecord(expected.characterClass);
        own.word2c = 10;
        own.agility = 290;
        own.equipment[LeftHand] = Item(SmallShield, 0, {{0x52, 9}});

        ASSERT_TRUE(Client::Scenes::UpdateOwnWord60(own, nullptr).HasValue());

        EXPECT_EQ(own.word60, expected.word60);
    }
}

// El lugar 1 (2 de 22: 0.5f) suma D+0x10 reducido (3 − 1) y sus 0x52 reducidas (9 − 4), y 0x5a por 10;
// después 0x5a por 10 de los lugares 2 a 6, 11 y 10, también sin durabilidad, y no del 8 ni del 9.
TEST(OwnStatHelpersTest, Word60AddsTheLeftHandAndTheOptions5a)
{
    const ItemTable items = ItemsWithBaseDurability({{SmallShield, 22}});
    OwnCharacterRecord own = EmptyRecord(DarkWizard);
    own.agility = 300;
    own.equipment[LeftHand] = Item(SmallShield, 2, {{0x52, 9}, {0x5a, 0}});
    own.equipment[LeftHand].byte10 = 3;
    own.equipment[Helm] = Item(BronzeHelm, 0, {{0x5a, 0}});
    own.equipment[Slot10] = Item(TypeOf(13, 8), 1, {{0x5a, 0}});
    own.equipment[Pet] = Item(TypeOf(13, 0), 1, {{0x5a, 0}});
    own.equipment[Slot9] = Item(TypeOf(13, 12), 1, {{0x5a, 0}});

    ASSERT_TRUE(Client::Scenes::UpdateOwnWord60(own, &items).HasValue());

    EXPECT_EQ(own.word60, 140); // 102, 107, 117, 128, 140

    own.equipment[LeftHand].optionCount = 10;
    auto updated = Client::Scenes::UpdateOwnWord60(own, &items);
    ASSERT_FALSE(updated.HasValue());
    EXPECT_EQ(own.word60, 102);

    updated = Client::Scenes::UpdateOwnWord60(own, nullptr);
    ASSERT_FALSE(updated.HasValue());
    EXPECT_NE(updated.Error().find("Item.bmd"), std::string::npos);
    EXPECT_EQ(own.word60, 100);
}

// ---- 0x550990 ----

TEST(OwnStatHelpersTest, Word62StartsFromTheClassAndTheAgility)
{
    struct Case
    {
        std::uint8_t characterClass;
        std::uint16_t word62;
    };
    for (const Case& expected : {Case{DarkWizard, 75}, Case{DarkKnight, 100}, Case{Elf, 30}, Case{MagicGladiator, 60},
                                 Case{DarkLord, 42}, Case{6, 60}})
    {
        SCOPED_TRACE(expected.characterClass);
        OwnCharacterRecord own = EmptyRecord(expected.characterClass);
        own.word2c = 10;
        own.agility = 290;
        ItemSetOptionList setOptions;

        ASSERT_TRUE(Client::Scenes::UpdateOwnWord62(own, nullptr, {}, &setOptions).HasValue());

        EXPECT_EQ(own.word62, expected.word62);
    }
}

// Los lugares 1 a 7 con durabilidad: D+0x12 más sus 0x53, reducido. El casco (3 de 34: 0.5f) 14 − 7; las
// botas, sin desgaste, 7; las alas (40 de 200) con 0 y 0 en lugar de sus D+0x1b y D+0x1c: 0.3f y 10 − 3
// (con su D+0x1c en 1 serían 0.5f y 5). La armadura, sin durabilidad, no.
TEST(OwnStatHelpersTest, Word62AddsTheReducedWord12OfSlots1To7)
{
    const ItemTable items = ItemsWithBaseDurability({{BronzeHelm, 34}, {BronzeBoots, 34}, {WingsOfElf, 200}});
    OwnCharacterRecord own = EmptyRecord(DarkWizard);
    own.agility = 300;
    own.equipment[Helm] = Item(BronzeHelm, 3, {{0x53, 4}});
    own.equipment[Helm].word12 = 10;
    own.equipment[Armor] = Item(TypeOf(8, 0), 0);
    own.equipment[Armor].word12 = 50;
    own.equipment[Boots] = Item(BronzeBoots, 34);
    own.equipment[Boots].word12 = 7;
    own.equipment[Wings] = Item(WingsOfElf, 40);
    own.equipment[Wings].word12 = 10;
    own.equipment[Wings].flags1c = 1;
    ItemSetOptionList setOptions;

    ASSERT_TRUE(Client::Scenes::UpdateOwnWord62(own, &items, {}, &setOptions).HasValue());

    EXPECT_EQ(own.word62, 75 + 7 + 7 + 7);
}

// [0x743a17c] distinto de 0: w + _ftol(w · f), con 0.05f, 0.1f, 0.15f o 0.2f para [0x743a178] de 10 a 13
// y 0 para los otros (la resta de 10 envuelve sin signo).
TEST(OwnStatHelpersTest, Word62AddsTheScanOf505820)
{
    struct Case
    {
        Scan505820 scan;
        std::uint16_t word62;
    };
    for (const Case& expected : {Case{{10, 1}, 93}, Case{{11, 1}, 97}, Case{{12, 1}, 102}, Case{{13, 1}, 106},
                                 Case{{14, 1}, 89}, Case{{9, 1}, 89}, Case{{-1, 1}, 89}, Case{{12, 0}, 89}})
    {
        SCOPED_TRACE(expected.scan.dword743a178);
        OwnCharacterRecord own = EmptyRecord(DarkWizard);
        own.agility = 356; // 89
        ItemSetOptionList setOptions;

        ASSERT_TRUE(Client::Scenes::UpdateOwnWord62(own, nullptr, expected.scan, &setOptions).HasValue());

        EXPECT_EQ(own.word62, expected.word62);
    }
}

// 0x96 (5), A · 10 / 100 (0xa6) y V · 20 / 100 (0xa7) y, con un tipo de 0xc00 a 0xdff en el lugar 1, el
// 50 % de 0xa2. Un DK con A = 30 (10) y V = 50.
TEST(OwnStatHelpersTest, Word62AddsTheSetOptions)
{
    struct Case
    {
        std::uint16_t leftType;
        std::uint16_t word62;
    };
    for (const Case& expected : {Case{SmallShield, 42}, Case{TypeOf(6, 511), 42}, Case{BronzeHelm, 28},
                                 Case{EmptyType, 28}, Case{SkullStaff, 28}})
    {
        SCOPED_TRACE(expected.leftType);
        OwnCharacterRecord own = EmptyRecord(DarkKnight);
        own.agility = 30;
        own.word2e = 5;
        own.vitality = 45;
        own.equipment[LeftHand] = Item(expected.leftType, 0);
        ItemSetOptionList setOptions = SetOptionsOf({{0x0a, 0, 5}, {0x1a, 1, 10}, {0x1b, 1, 20}, {0x16, 0, 50}});

        ASSERT_TRUE(Client::Scenes::UpdateOwnWord62(own, nullptr, {}, &setOptions).HasValue());

        EXPECT_EQ(own.word62, expected.word62);
    }
}

// Las 0x96 del lugar 8 y, si las alas son Cape of Lord, las suyas.
TEST(OwnStatHelpersTest, Word62AddsThePetAndTheCapeOfLord)
{
    const ItemTable items = ItemsWithBaseDurability({{CapeOfLord, 200}, {WingsOfElf, 200}});
    for (const std::uint16_t wings : {CapeOfLord, WingsOfElf})
    {
        SCOPED_TRACE(wings);
        OwnCharacterRecord own = EmptyRecord(Elf);
        own.agility = 300;
        own.equipment[Pet] = Item(TypeOf(13, 0), 1, {{0x96, 2}});
        own.equipment[Wings] = Item(wings, 1, {{0x96, 3}});
        ItemSetOptionList setOptions;

        ASSERT_TRUE(Client::Scenes::UpdateOwnWord62(own, &items, {}, &setOptions).HasValue());

        EXPECT_EQ(own.word62, wings == CapeOfLord ? 35 : 32);
    }
}

// Un lugar con durabilidad y el tipo −1 lee fuera de Item.bmd (como Q-C184): error, con +0x62 en la base.
TEST(OwnStatHelpersTest, Word62StopsWhereItReadsWhatMuRetLacks)
{
    const ItemTable items = ItemsWithBaseDurability({});
    OwnCharacterRecord own = EmptyRecord(DarkWizard);
    own.agility = 300;
    own.equipment[Armor] = Item(EmptyType, 5);
    ItemSetOptionList setOptions;

    auto updated = Client::Scenes::UpdateOwnWord62(own, &items, {}, &setOptions);
    ASSERT_FALSE(updated.HasValue());
    EXPECT_NE(updated.Error().find("Q-C184"), std::string::npos);
    EXPECT_EQ(own.word62, 75);

    own.equipment[Armor] = Item(TypeOf(8, 0), 5);
    updated = Client::Scenes::UpdateOwnWord62(own, nullptr, {}, &setOptions);
    ASSERT_FALSE(updated.HasValue());
    EXPECT_NE(updated.Error().find("Item.bmd"), std::string::npos);

    updated = Client::Scenes::UpdateOwnWord62(own, &items, {12, 1}, nullptr);
    ASSERT_FALSE(updated.HasValue());
    EXPECT_NE(updated.Error().find("0x40e1b0"), std::string::npos);
    EXPECT_EQ(own.word62, 75 + 11); // la armadura suma 0 y el 15 % de 75 es 11
}

// ---- 0x550c30 y 0x550c60 ----

// +0x64 es el D+0x14 del último de los lugares 2 a 7 con durabilidad (0 con el tipo −1); sin ninguno queda.
TEST(OwnStatHelpersTest, Word64IsWord14OfTheLastBodySlotWithDurability)
{
    OwnCharacterRecord own = EmptyRecord(DarkWizard);
    own.word64 = 0x1234;
    own.equipment[RightHand] = Item(Kris, 1);
    own.equipment[RightHand].word14 = 99;
    own.equipment[Pet] = Item(TypeOf(13, 0), 1);
    own.equipment[Pet].word14 = 98;
    Client::Scenes::UpdateOwnWord64(own);
    EXPECT_EQ(own.word64, 0x1234);

    own.equipment[Helm] = Item(BronzeHelm, 1);
    own.equipment[Helm].word14 = 11;
    own.equipment[Armor] = Item(TypeOf(8, 0), 0);
    own.equipment[Armor].word14 = 22;
    own.equipment[Pants] = Item(TypeOf(9, 0), 3);
    own.equipment[Pants].word14 = 33;
    Client::Scenes::UpdateOwnWord64(own);
    EXPECT_EQ(own.word64, 33);

    own.equipment[Wings].durability = 1;
    Client::Scenes::UpdateOwnWord64(own);
    EXPECT_EQ(own.word64, 0);
}

// +0x66 es el D+0x18 de las botas con durabilidad más el de las alas con durabilidad; sin las botas, el de
// las alas se suma a lo que había.
TEST(OwnStatHelpersTest, Word66IsWord18OfTheBootsPlusTheWings)
{
    OwnCharacterRecord own = EmptyRecord(DarkKnight);
    own.word66 = 1000;
    own.equipment[Boots] = Item(BronzeBoots, 2);
    own.equipment[Boots].word18 = 7;
    own.equipment[Wings] = Item(WingsOfElf, 1);
    own.equipment[Wings].word18 = 5;
    Client::Scenes::UpdateOwnWord66(own);
    EXPECT_EQ(own.word66, 12);

    own.equipment[Boots].durability = 0;
    Client::Scenes::UpdateOwnWord66(own);
    EXPECT_EQ(own.word66, 17);

    own.equipment[Boots] = Item(EmptyType, 4);
    own.equipment[Boots].word18 = 7;
    Client::Scenes::UpdateOwnWord66(own);
    EXPECT_EQ(own.word66, 5);

    own.equipment[Boots].durability = 0;
    own.equipment[Wings].durability = 0;
    Client::Scenes::UpdateOwnWord66(own);
    EXPECT_EQ(own.word66, 5);
}
} // namespace
