#include "Scenes/OwnDerivedStats.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/ItemSetOptionList.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnStatHelpers.h"
#include "World/ItemState.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Client::Scenes::RecalculateOwnStats y los pasos de 0x550d10 que MuRet tiene (06, "Los stats del
// registro propio"; 07, Q-C184, Q-C185 y Q-C187). Los valores salen del ASM cotejado con el PE c32bbd5f, y los
// rand() del modelo de la CRT; ninguno de una corrida del original.
namespace
{
using Client::Data::Tables::ItemSetTables;
using Client::Render::Models::CrtRandom;
using Client::Render::Models::OwnRecordCloseRandoms;
using Client::Scenes::ActorState;
using Client::Scenes::ItemSetOptionList;
using Client::Scenes::ItemSetState;
using Client::Scenes::OwnCharacterRecord;
using Client::Tests::AfterDraws;
using Client::Tests::FirstSeedWhere;
using Client::World::ItemState;

constexpr std::size_t KindOffset = 0x44;
constexpr std::size_t WingsSlot = 7;
constexpr std::uint16_t CapeOfLord = 0x1a1e;
constexpr std::uint16_t EmptyType = 0xffff;

// Un tipo de Item.bmd y su byte +0x44. Agregado y no std::pair: el constructor de pair desde int
// estrecha dentro de <utility> (C4242 en MSVC).
struct TypeKind
{
    std::uint16_t type;
    std::uint8_t kind;
};

// Item.bmd sintético: el byte +0x44 de cada tipo de `kinds` y todo lo demás en 0.
Client::Data::Tables::ItemTable ItemsWithKinds(std::initializer_list<TypeKind> kinds)
{
    std::vector<std::byte> plain(Client::Data::Tables::ItemTableSize);
    for (const auto& [type, kind] : kinds)
    {
        plain[type * Client::Data::Tables::ItemRecordSize + KindOffset] = std::byte{kind};
    }
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Data::Tables::ItemRecordSize);
    return Client::Data::Tables::ItemTable(std::span<const std::byte>(encrypted));
}

// Un ítem con durabilidad y el valor `value` en los bits 2 y 3 del byte +0x1c.
ItemState Worn(std::uint16_t type, std::uint8_t value, std::uint8_t durability = 1)
{
    ItemState item;
    item.id = type;
    item.durability = durability;
    item.flags1c = static_cast<std::uint8_t>(value << 2);
    return item;
}

// Lo que 0x550d10 lee y escribe fuera del registro, con la cuenta de accesos en 0: cada cierre saca
// 3.848 rand().
class OwnStatsFixture : public ::testing::Test
{
protected:
    Client::Scenes::OwnStatsContext Context(const ItemSetTables* tables, const Client::Data::Tables::ItemTable* items)
    {
        return {.itemSetTables = tables,
                .itemSets = &m_itemSets,
                .items = items,
                .publishedHero = m_hero,
                .ownRecordAccesses = m_accesses,
                .random = m_random};
    }

    ItemSetState m_itemSets;
    ActorState m_hero;
    std::uint8_t m_accesses = 0;
    CrtRandom m_random{Seed};

    static constexpr std::uint32_t Seed = 13;
};

// 0x550d24: lo primero es 0x40e1b0. Sin las tablas de los conjuntos (0x40c600) corta antes de tocar
// nada: ni el registro, ni [0x6eed3c], ni el héroe, ni un rand().
TEST_F(OwnStatsFixture, WithoutTheSetTablesTheRecalculationStopsBeforeTouchingAnything)
{
    OwnCharacterRecord own;
    own.word2a = 0x1234;
    own.word36 = 0x2345;
    own.wordef8 = 0x3456;
    own.bytef02 = 7;
    m_itemSets.list.used[0] = 1;
    m_hero.byte358 = 4;

    const auto recalculated = Client::Scenes::RecalculateOwnStats(own, Context(nullptr, nullptr));

    ASSERT_FALSE(recalculated.HasValue());
    EXPECT_NE(recalculated.Error().find("0x40e1b0"), std::string::npos);
    EXPECT_NE(recalculated.Error().find("0x40c600"), std::string::npos);
    EXPECT_EQ(own.word2a, 0x1234);
    EXPECT_EQ(own.word36, 0x2345);
    EXPECT_EQ(own.wordef8, 0x3456);
    EXPECT_EQ(own.bytef02, 7);
    EXPECT_EQ(m_itemSets.list.used[0], 1);
    EXPECT_EQ(m_hero.byte358, 4);
    EXPECT_EQ(m_random.State(), CrtRandom(Seed).State());
}

// 0x550d10 en orden hasta 0x550990: 0x40e1b0 (dos piezas del conjunto 1, con las opciones 0 y 4), las
// marcas de +0x937c en 0, 0x54ef00, los doce lugares (el tipo 1 suma a +0x2c), 0x40da20 con 0x8c a
// 0x90, Cape of Lord y los cinco primeros helpers. La opción 0 ya la marcó 0x40de10 sobre las copias:
// si las marcas no volvieran a 0, +0x2a no la sumaría. Sin lo de 0x505820 corta en 0x550990, sin la
// cola: 33 cierres de 0x40e1b0 y el de la capa, y ningún rand() más.
TEST_F(OwnStatsFixture, TheRecalculationRunsInOrderUntil0x550990)
{
    std::vector<std::byte> types(Client::Data::Tables::ItemSetTypeTableSize, std::byte{0xff});
    types[1 * Client::Data::Tables::ItemSetTypeRecordSize] = std::byte{1};
    std::vector<std::byte> options(Client::Data::Tables::ItemSetOptionTableSize, std::byte{0xff});
    const std::span<std::byte> set1 = std::span(options).subspan(Client::Data::Tables::ItemSetOptionRecordSize,
                                                                 Client::Data::Tables::ItemSetOptionRecordSize);
    set1[0] = std::byte{'U'};
    set1[1] = std::byte{0};
    set1[0x20] = std::byte{0}; // la opción 0 (0x8c, +0x2a) con 7
    set1[0x21] = std::byte{4}; // la opción 4 (0x90, +0x36) con 9
    set1[0x2c] = std::byte{7};
    set1[0x2d] = std::byte{9};
    const ItemSetTables tables = Client::Tests::ItemSetTablesFrom(std::move(types), std::move(options));
    const auto items = ItemsWithKinds({{1, 2}});
    OwnCharacterRecord own;
    own.equipment.fill(Worn(EmptyType, 0, 0));
    own.equipment[2] = Worn(1, 1);
    own.equipment[2].flags1c = 0x05; // el valor 1 en los bits 2 y 3, y el conjunto del byte 0
    own.equipment[3] = own.equipment[2];
    ItemState& wings = own.equipment[WingsSlot];
    wings = Worn(CapeOfLord, 0);
    wings.optionCount = 1;
    wings.optionBytes[0] = 0x90;
    wings.optionBytes[8] = 20;
    own.word2a = 0x1234;
    own.word32 = 0x55;
    own.word36 = 0x2345;
    own.wordef8 = 0x3456;
    own.bytef03 = 6;
    m_hero.byte358 = 4;

    const auto recalculated = Client::Scenes::RecalculateOwnStats(own, Context(&tables, &items));

    ASSERT_FALSE(recalculated.HasValue());
    EXPECT_NE(recalculated.Error().find("0x550990"), std::string::npos);
    EXPECT_EQ(m_itemSets.list.count9348, 2);
    EXPECT_EQ(m_itemSets.list.used[0], 1);
    EXPECT_EQ(m_itemSets.list.used[1], 1);
    EXPECT_EQ(m_hero.byte358, 0);
    EXPECT_EQ(own.word2a, 7);
    EXPECT_EQ(own.word2c, 10);
    EXPECT_EQ(own.word32, 0);
    EXPECT_EQ(own.word36, 9 + 20);
    EXPECT_EQ(own.wordef8, 0x3456);
    EXPECT_EQ(own.bytef03, 6);
    EXPECT_EQ(m_accesses, 0);
    EXPECT_EQ(m_random.State(), AfterDraws(Seed, 34 * OwnRecordCloseRandoms).State());
}

// Con lo de 0x505820 (acá, [0x743a17c] en 0), 0x550d10 llega al final: los ocho helpers en su orden y la
// cola. Sin equipo ni conjuntos, 0x40e1b0 cierra 33 veces el registro; la cola saca sus tres rand(): el
// del IDIV con +0xee2 = +0xee4 = 0 (divide por 1) y los dos % 100. +0xef8 y +0xefa son restas de
// WORD en 0, y +0xefc es 0 − +0x62.
TEST_F(OwnStatsFixture, WithTheScanOf0x505820TheRecalculationRunsTheHelpersAndTheTail)
{
    const ItemSetTables tables = Client::Tests::ItemSetTablesFrom(
        std::vector<std::byte>(Client::Data::Tables::ItemSetTypeTableSize, std::byte{0xff}),
        std::vector<std::byte>(Client::Data::Tables::ItemSetOptionTableSize, std::byte{0xff}));
    const auto items = ItemsWithKinds({});
    OwnCharacterRecord own;
    own.equipment.fill(Worn(EmptyType, 0, 0));
    own.wordef8 = 0x3456;
    own.wordefa = 0x4567;
    Client::Scenes::OwnStatsContext context = Context(&tables, &items);
    context.scan505820 = Client::Scenes::Scan505820{};

    const auto recalculated = Client::Scenes::RecalculateOwnStats(own, context);

    ASSERT_TRUE(recalculated.HasValue()) << recalculated.Error();
    EXPECT_EQ(own.wordef8, 0);
    EXPECT_EQ(own.wordefa, 0);
    EXPECT_EQ(own.wordefc, static_cast<std::uint16_t>(0u - own.word62));
    EXPECT_EQ(m_accesses, 0);
    EXPECT_EQ(m_random.State(), AfterDraws(Seed, 33 * OwnRecordCloseRandoms + 3).State());
}

// 0x550dc2..0x550e12: +0x2a con 0x8c, +0x2c con 0x8d, +0x30 con 0x8e, +0x2e con 0x8f y +0x36 con 0x90;
// +0x32 y +0x34 no tienen opción de conjunto. Con más de 16 opciones, error en la primera llamada.
TEST(OwnDerivedStatsTest, EachWordTakesTheSetOptionsOfItsSelector)
{
    ItemSetOptionList list;
    list.count9348 = 4;
    list.count9349 = 2;
    for (std::size_t index = 0; index < 6; ++index)
    {
        list.entries[index][0] = static_cast<std::uint8_t>(index);
        list.values[index] = static_cast<std::int32_t>(index + 1);
    }
    OwnCharacterRecord own;
    own.word2a = 0x100;
    own.word32 = 0x200;
    own.word34 = 0x300;

    ASSERT_TRUE(Client::Scenes::AddOwnItemSetOptionValues(own, list).HasValue());

    EXPECT_EQ(own.word2a, 0x101);
    EXPECT_EQ(own.word2c, 2);
    EXPECT_EQ(own.word30, 3);
    EXPECT_EQ(own.word2e, 4);
    EXPECT_EQ(own.word36, 5);
    EXPECT_EQ(own.word32, 0x200);
    EXPECT_EQ(own.word34, 0x300);
    EXPECT_EQ(list.used[4], 1);
    EXPECT_EQ(list.used[5], 0);

    list.count9348 = 16;
    list.count9349 = 1;
    const auto outside = Client::Scenes::AddOwnItemSetOptionValues(own, list);
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("0x40da20 con 0x8c"), std::string::npos);
    EXPECT_EQ(own.word2a, 0x101);
}

// 0x54ef00: los siete WORD de +0x2a a +0x36 en 0; +0x28 y +0x38, los de al lado, quedan.
TEST(OwnDerivedStatsTest, TheClearZeroesTheSevenItemStatWordsOnly)
{
    OwnCharacterRecord own;
    own.maxMana = 0x28;
    own.word2a = 1;
    own.word2c = 2;
    own.word2e = 3;
    own.word30 = 4;
    own.word32 = 5;
    own.word34 = 6;
    own.word36 = 7;
    own.bp = 0x38;

    Client::Scenes::ClearOwnItemStatWords(own);

    EXPECT_EQ(own.word2a, 0);
    EXPECT_EQ(own.word2c, 0);
    EXPECT_EQ(own.word2e, 0);
    EXPECT_EQ(own.word30, 0);
    EXPECT_EQ(own.word32, 0);
    EXPECT_EQ(own.word34, 0);
    EXPECT_EQ(own.word36, 0);
    EXPECT_EQ(own.maxMana, 0x28);
    EXPECT_EQ(own.bp, 0x38);
}

// 0x40dc50: el valor es (D+0x1c >> 2) & 3, sin los otros bits, y el tipo el byte +0x44 del registro de
// Item.bmd del tipo del ítem.
TEST(OwnDerivedStatsTest, TheItemBonusIsBits2And3OfByte1cAndTheKindAt44)
{
    const auto items = ItemsWithKinds({{0x10, 3}, {0x1fff, 0xa7}});
    ItemState item;
    item.id = 0x10;
    item.flags1c = 0xf3; // 1111 0011: los bits 2 y 3 en 0
    auto bonus = Client::Scenes::ItemStatBonusOf(item, items);
    ASSERT_TRUE(bonus.HasValue());
    EXPECT_EQ(bonus.Value().kind, 3);
    EXPECT_EQ(bonus.Value().value, 0);

    item.flags1c = 0x0c;
    bonus = Client::Scenes::ItemStatBonusOf(item, items);
    ASSERT_TRUE(bonus.HasValue());
    EXPECT_EQ(bonus.Value().value, 3);

    // El último tipo de la tabla, 8191, todavía está adentro.
    item.id = 0x1fff;
    item.flags1c = 0x18;
    bonus = Client::Scenes::ItemStatBonusOf(item, items);
    ASSERT_TRUE(bonus.HasValue());
    EXPECT_EQ(bonus.Value().kind, 0xa7);
    EXPECT_EQ(bonus.Value().value, 2);
}

// Q-C184: movsx y la tabla de 8.192: con −1 (un lugar vaciado), un tipo negativo o uno de 8.192 o más, el
// original lee memoria que no es de la tabla. MuRet no inventa ese byte.
TEST(OwnDerivedStatsTest, ATypeOutsideItemBmdIsAnExplicitError)
{
    const auto items = ItemsWithKinds({});
    struct Outside
    {
        std::uint16_t type;
        std::string shown;
    };
    for (const Outside& outside :
         {Outside{EmptyType, "el tipo -1 "}, Outside{0x8000, "el tipo -32768 "}, Outside{0x2000, "el tipo 8192 "}})
    {
        SCOPED_TRACE(outside.shown);
        const auto bonus = Client::Scenes::ItemStatBonusOf(Worn(outside.type, 1), items);
        ASSERT_FALSE(bonus.HasValue());
        EXPECT_NE(bonus.Error().find("Q-C184"), std::string::npos);
        EXPECT_NE(bonus.Error().find(outside.shown), std::string::npos);
    }
}

// 0x550d49..0x550db1: los doce lugares, también del 9 al 11, que 0x512a30 no lee; sin durabilidad no
// llama a 0x40dc50. Los tipos 1 a 4 suman 5 · valor a +0x2a, +0x2c, +0x30 y +0x2e (la tabla de saltos
// de 0x5513b4), en 16 bits; los otros, nada.
TEST(OwnDerivedStatsTest, BonusesAddFiveTimesTheValueToTheirWordInAllTwelveSlots)
{
    const auto items = ItemsWithKinds({{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 0}, {6, 5}});
    OwnCharacterRecord own;
    for (ItemState& item : own.equipment)
    {
        item = Worn(EmptyType, 3, 0);
    }
    own.equipment[0] = Worn(1, 3);
    own.equipment[1] = Worn(2, 2);
    own.equipment[2] = Worn(3, 1);
    own.equipment[3] = Worn(4, 3);
    own.equipment[4] = Worn(5, 3);
    own.equipment[5] = Worn(6, 3);
    own.equipment[10] = Worn(4, 2);
    own.equipment[11] = Worn(1, 3, 0x80);
    own.word2a = 0xfff6;
    own.word32 = 0x32;

    ASSERT_TRUE(Client::Scenes::AddOwnItemStatBonuses(own, &items).HasValue());

    EXPECT_EQ(own.word2a, 20); // 0xfff6 + 15 envuelve a 5, y el lugar 11 suma otros 15
    EXPECT_EQ(own.word2c, 10);
    EXPECT_EQ(own.word30, 5);
    EXPECT_EQ(own.word2e, 25);
    EXPECT_EQ(own.word32, 0x32);
    EXPECT_EQ(own.word34, 0);
    EXPECT_EQ(own.word36, 0);
}

// Un lugar que corta deja lo que sumaron los anteriores, y no sigue. Sin Item.bmd, corta en el primer
// lugar que la lee; si ninguno tiene durabilidad, ninguno la lee.
TEST(OwnDerivedStatsTest, ABonusErrorKeepsWhatTheEarlierSlotsAdded)
{
    const auto items = ItemsWithKinds({{1, 1}, {2, 2}});
    OwnCharacterRecord own;
    own.equipment.fill(Worn(EmptyType, 0, 0));
    own.equipment[0] = Worn(1, 1);
    own.equipment[1] = Worn(EmptyType, 1);
    own.equipment[2] = Worn(2, 1);

    const auto added = Client::Scenes::AddOwnItemStatBonuses(own, &items);

    ASSERT_FALSE(added.HasValue());
    EXPECT_NE(added.Error().find("lugar 1"), std::string::npos);
    EXPECT_NE(added.Error().find("Q-C184"), std::string::npos);
    EXPECT_EQ(own.word2a, 5);
    EXPECT_EQ(own.word2c, 0);

    OwnCharacterRecord bare;
    bare.equipment.fill(Worn(1, 3, 0));
    EXPECT_TRUE(Client::Scenes::AddOwnItemStatBonuses(bare, nullptr).HasValue());
    bare.equipment[4].durability = 1;
    const auto unloaded = Client::Scenes::AddOwnItemStatBonuses(bare, nullptr);
    ASSERT_FALSE(unloaded.HasValue());
    EXPECT_NE(unloaded.Error().find("Item.bmd"), std::string::npos);
    EXPECT_NE(unloaded.Error().find("lugar 4"), std::string::npos);
}

// 0x550e17..0x551284: con 0x1a1e en el lugar 7, abre el registro, 0x54e540 suma los valores de las
// opciones 0x90 a +0x36 (con durabilidad, 16 bits) y lo cierra: con la cuenta en 0, 3.848 rand(); con
// el registro abierto por quien llama, como en 0x512a30, ninguno.
TEST(OwnDerivedStatsTest, TheCapeOfLordAddsItsOption90ToWord36AndClosesTheRecord)
{
    constexpr std::uint32_t Seed = 7;
    OwnCharacterRecord own;
    own.word36 = 0xfff0;
    ItemState& wings = own.equipment[WingsSlot];
    wings.id = CapeOfLord;
    wings.durability = 1;
    wings.optionCount = 3;
    wings.optionBytes = {0x90, 0x8c, 0x90, 0, 0, 0, 0, 0, 10, 20, 30};
    std::uint8_t accesses = 0;
    CrtRandom random(Seed);

    ASSERT_TRUE(Client::Scenes::AddCapeOfLordOptions(own, accesses, random).HasValue());
    EXPECT_EQ(own.word36, 24); // 0xfff0 + 10 + 30
    EXPECT_EQ(accesses, 0);
    EXPECT_EQ(random.State(), AfterDraws(Seed, OwnRecordCloseRandoms).State());

    accesses = 1;
    random = CrtRandom(Seed);
    ASSERT_TRUE(Client::Scenes::AddCapeOfLordOptions(own, accesses, random).HasValue());
    EXPECT_EQ(own.word36, 64);
    EXPECT_EQ(accesses, 1);
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());

    // Sin durabilidad 0x54e540 no suma, pero el registro se abre y se cierra igual.
    wings.durability = 0;
    random = CrtRandom(Seed);
    accesses = 0;
    ASSERT_TRUE(Client::Scenes::AddCapeOfLordOptions(own, accesses, random).HasValue());
    EXPECT_EQ(own.word36, 64);
    EXPECT_EQ(random.State(), AfterDraws(Seed, OwnRecordCloseRandoms).State());
}

// Con otro tipo en las alas, aunque tenga la opción 0x90, ni abre el registro ni suma.
TEST(OwnDerivedStatsTest, OtherWingsDoNotOpenTheRecord)
{
    OwnCharacterRecord own;
    ItemState& wings = own.equipment[WingsSlot];
    wings.durability = 1;
    wings.optionCount = 1;
    wings.optionBytes[0] = 0x90;
    wings.optionBytes[8] = 50;
    for (const std::uint16_t type : {std::uint16_t{0x1a1d}, std::uint16_t{0x1a1f}, std::uint16_t{0x1800}, EmptyType})
    {
        SCOPED_TRACE(type);
        wings.id = type;
        std::uint8_t accesses = 0;
        CrtRandom random(3);
        ASSERT_TRUE(Client::Scenes::AddCapeOfLordOptions(own, accesses, random).HasValue());
        EXPECT_EQ(own.word36, 0);
        EXPECT_EQ(accesses, 0);
        EXPECT_EQ(random.State(), CrtRandom(3).State());
    }
}

// Más de nueve opciones: 0x54e540 seguiría leyendo en el lugar 8, fuera de la proyección. Error, después
// de abrir, y con el acceso cerrado.
TEST(OwnDerivedStatsTest, ACapeWithMoreThanNineOptionsIsAnExplicitErrorWithTheRecordClosed)
{
    OwnCharacterRecord own;
    ItemState& wings = own.equipment[WingsSlot];
    wings.id = CapeOfLord;
    wings.durability = 1;
    wings.optionCount = 10;
    std::uint8_t accesses = 0;
    CrtRandom random(5);

    const auto added = Client::Scenes::AddCapeOfLordOptions(own, accesses, random);

    ASSERT_FALSE(added.HasValue());
    EXPECT_NE(added.Error().find("10 opciones"), std::string::npos);
    EXPECT_EQ(accesses, 0);
    EXPECT_EQ(random.State(), AfterDraws(5, OwnRecordCloseRandoms).State());
}

// La cola con un rango de un solo valor: el resto es 0 sea cual sea el rand(). Las restas envuelven, el
// tope de +0xefe deja 100 (y +0xf02 en 1 con cualquier sorteo) y +0xf00 en 0 deja +0xf03 en 0. Saca tres.
TEST(OwnDerivedStatsTest, TheTailSubtractsWithWrapAndDrawsThreeRandoms)
{
    constexpr std::uint32_t Seed = 11;
    OwnCharacterRecord own;
    own.wordef0 = 5;
    own.wordef2 = 0x10;
    own.wordee6 = 7;
    own.wordee2 = 300;
    own.wordee4 = 300;
    own.word62 = 0x200;
    own.word4e = 3;
    own.wordeec = 5;
    own.word60 = 7;
    own.wordeea = 7;
    own.bytef02 = 9;
    own.bytef03 = 9;
    CrtRandom random(Seed);

    ASSERT_TRUE(Client::Scenes::UpdateOwnDerivedStatsTail(own, random).HasValue());

    EXPECT_EQ(own.wordef8, 0xfffe);
    EXPECT_EQ(own.wordefa, 9);
    EXPECT_EQ(own.wordefc, 0xff2c); // 300 − 0x200
    EXPECT_EQ(own.wordefe, 100);    // 3 − 5 = 0xfffe, sin signo, al tope
    EXPECT_EQ(own.wordf00, 0);
    EXPECT_EQ(own.bytef02, 1);
    EXPECT_EQ(own.bytef03, 0);
    EXPECT_EQ(random.State(), AfterDraws(Seed, 3).State());
}

// El tope es "más de 100" (jbe): 100 queda y 101 baja. Los sorteos comparan rand() % 100 con < (jge al
// revés): con 50, un 49 da 1 y un 50 da 0.
TEST(OwnDerivedStatsTest, TheTailCapsAboveOneHundredAndComparesStrictly)
{
    const std::uint32_t seed = FirstSeedWhere(
        [](CrtRandom& random)
        {
            static_cast<void>(random.Next());
            return random.Next() % 100 == 49 && random.Next() % 100 == 50;
        });
    OwnCharacterRecord own;
    own.word4e = 150;
    own.wordeec = 100;
    own.word60 = 60;
    own.wordeea = 10;
    CrtRandom random(seed);
    ASSERT_TRUE(Client::Scenes::UpdateOwnDerivedStatsTail(own, random).HasValue());
    EXPECT_EQ(own.wordefe, 50);
    EXPECT_EQ(own.wordf00, 50);
    EXPECT_EQ(own.bytef02, 1);
    EXPECT_EQ(own.bytef03, 0);

    own.word4e = 100;
    own.wordeec = 0;
    own.word60 = 101;
    own.wordeea = 0;
    ASSERT_TRUE(Client::Scenes::UpdateOwnDerivedStatsTail(own, random).HasValue());
    EXPECT_EQ(own.wordefe, 100);
    EXPECT_EQ(own.wordf00, 100);
}

// El rango usa los dos WORD sin signo y un IDIV con signo: de 10 a 19, rand() % 10 + 10; con +0xee2
// mayor que +0xee4 + 1 el divisor es negativo y el resto sigue al rand() (0 a 32.767).
TEST(OwnDerivedStatsTest, TheTailRangeIsASignedDivision)
{
    constexpr std::uint32_t Seed = 23;
    const int first = CrtRandom(Seed).Next();
    OwnCharacterRecord own;
    own.wordee2 = 10;
    own.wordee4 = 19;
    own.word62 = 4;
    CrtRandom random(Seed);
    ASSERT_TRUE(Client::Scenes::UpdateOwnDerivedStatsTail(own, random).HasValue());
    EXPECT_EQ(own.wordefc, static_cast<std::uint16_t>(first % 10 + 10 - 4));

    // 0 − 65.535 + 1 = −65.534: el resto es el rand() entero, y + 65.535 en 16 bits es − 1.
    own.wordee2 = 0xffff;
    own.wordee4 = 0;
    own.word62 = 0;
    random = CrtRandom(Seed);
    ASSERT_TRUE(Client::Scenes::UpdateOwnDerivedStatsTail(own, random).HasValue());
    EXPECT_EQ(own.wordefc, static_cast<std::uint16_t>(first - 1));

    // 0xffff − 0 + 1 = 65.536: tampoco limita el rand().
    own.wordee2 = 0;
    own.wordee4 = 0xffff;
    random = CrtRandom(Seed);
    ASSERT_TRUE(Client::Scenes::UpdateOwnDerivedStatsTail(own, random).HasValue());
    EXPECT_EQ(own.wordefc, static_cast<std::uint16_t>(first));
}

// Q-C185: con +0xee2 = +0xee4 + 1 el IDIV de 0x551306 divide por 0. Ya están +0xef8, +0xefa y el primer
// rand(); lo demás queda.
TEST(OwnDerivedStatsTest, TheTailDividesByZeroAsAnExplicitError)
{
    OwnCharacterRecord own;
    own.wordef0 = 9;
    own.wordee6 = 1;
    own.wordee2 = 5;
    own.wordee4 = 4;
    own.wordefc = 0x1111;
    own.wordefe = 0x2222;
    own.bytef02 = 3;
    CrtRandom random(2);

    const auto tail = Client::Scenes::UpdateOwnDerivedStatsTail(own, random);

    ASSERT_FALSE(tail.HasValue());
    EXPECT_NE(tail.Error().find("Q-C185"), std::string::npos);
    EXPECT_EQ(own.wordef8, 8);
    EXPECT_EQ(own.wordefa, 0xffff);
    EXPECT_EQ(own.wordefc, 0x1111);
    EXPECT_EQ(own.wordefe, 0x2222);
    EXPECT_EQ(own.bytef02, 3);
    EXPECT_EQ(random.State(), AfterDraws(2, 1).State());
}
} // namespace
