#include "Scenes/OwnItemSets.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/ItemSetOptionList.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnDerivedStats.h"
#include "World/ItemState.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::Scenes: 0x40e1b0 y lo que llama, 0x40cfa0, 0x40d010, 0x40de10 y 0x40da20 (06, "Los stats del
// registro propio"; 07, Q-C184 y Q-C187). Las tablas son sintéticas; los valores salen del ASM cotejado
// con el PE c32bbd5f y los rand() del modelo de la CRT, ninguno de una corrida del original.
namespace
{
using Client::Data::Tables::ItemSetOptionRecordSize;
using Client::Data::Tables::ItemSetOptionTableSize;
using Client::Data::Tables::ItemSetTables;
using Client::Data::Tables::ItemSetTypeRecordSize;
using Client::Data::Tables::ItemSetTypeTableSize;
using Client::Render::Models::CrtRandom;
using Client::Render::Models::OwnRecordCloseRandoms;
using Client::Scenes::ActorState;
using Client::Scenes::ItemSetPiece;
using Client::Scenes::ItemSetPieces;
using Client::Scenes::ItemSetState;
using Client::Scenes::OwnCharacterRecord;
using Client::Scenes::OwnStatCopies;
using Client::World::ItemState;

constexpr std::uint8_t None = 0xff;
constexpr std::uint16_t EmptyType = 0xffff;

// ItemSetType.bmd en claro: todo en 0xff, como los tipos sin conjunto del archivo real.
class SetTypes
{
public:
    SetTypes() : m_plain(ItemSetTypeTableSize, std::byte{None}) {}

    // El byte `index` (0 a 3) del registro de `type`.
    SetTypes& Set(std::size_t type, std::size_t index, std::uint8_t setId)
    {
        m_plain[type * ItemSetTypeRecordSize + index] = std::byte{setId};
        return *this;
    }

    [[nodiscard]] std::vector<std::byte> Plain() const
    {
        return m_plain;
    }

private:
    std::vector<std::byte> m_plain;
};

// ItemSetOption.bmd en claro: los conjuntos sin usar en 0xff, como en el archivo real.
class SetOptions
{
public:
    SetOptions() : m_plain(ItemSetOptionTableSize, std::byte{None}) {}

    // Un conjunto con nombre, +0x3c en `fullPieces` y las marcas de las clases en 0, como los reales.
    SetOptions& Named(std::size_t set, std::string_view name, std::uint8_t fullPieces)
    {
        for (std::size_t i = 0; i < name.size(); ++i)
        {
            At(set, i) = static_cast<std::byte>(name[i]);
        }
        At(set, name.size()) = std::byte{0};
        At(set, 0x3c) = std::byte{fullPieces};
        for (std::size_t mark = 0x47; mark < ItemSetOptionRecordSize; ++mark)
        {
            At(set, mark) = std::byte{0};
        }
        return *this;
    }

    // El par `pair` (0 a 5): sus dos opciones y sus dos valores.
    SetOptions& Pair(std::size_t set, std::size_t pair, std::uint8_t first, std::uint8_t firstValue,
                     std::uint8_t second = None, std::uint8_t secondValue = None)
    {
        At(set, 0x20 + 2 * pair) = std::byte{first};
        At(set, 0x21 + 2 * pair) = std::byte{second};
        At(set, 0x2c + 2 * pair) = std::byte{firstValue};
        At(set, 0x2d + 2 * pair) = std::byte{secondValue};
        return *this;
    }

    // La opción `index` (0 a 4) del conjunto completo.
    SetOptions& Full(std::size_t set, std::size_t index, std::uint8_t option, std::uint8_t value)
    {
        At(set, 0x3d + index) = std::byte{option};
        At(set, 0x42 + index) = std::byte{value};
        return *this;
    }

    SetOptions& Byte(std::size_t set, std::size_t offset, std::uint8_t value)
    {
        At(set, offset) = std::byte{value};
        return *this;
    }

    [[nodiscard]] std::vector<std::byte> Plain() const
    {
        return m_plain;
    }

private:
    std::byte& At(std::size_t set, std::size_t offset)
    {
        return m_plain[set * ItemSetOptionRecordSize + offset];
    }

    std::vector<std::byte> m_plain;
};

ItemSetTables TablesOf(const SetTypes& types, const SetOptions& options)
{
    return Client::Tests::ItemSetTablesFrom(types.Plain(), options.Plain());
}

std::string NameIn(const std::array<char, Client::Scenes::ItemSetNameSize>& slot)
{
    return std::string(slot.data());
}

ItemSetPieces OnePiece(std::uint8_t setId, std::uint8_t count, std::uint8_t kind = 0)
{
    ItemSetPieces pieces{};
    pieces[0] = {.setId = setId, .count = count, .kind = kind};
    return pieces;
}

// Una opción de la lista y su valor. Agregado y no std::pair: el constructor de pair desde int estrecha
// dentro de <utility> (C4242 en MSVC).
struct ListedOption
{
    int option = 0;
    int value = 0;

    friend bool operator==(const ListedOption&, const ListedOption&) = default;
};

void PrintTo(const ListedOption& listed, std::ostream* out)
{
    *out << '(' << listed.option << ", " << listed.value << ')';
}

using Listed = std::vector<ListedOption>;

// La lista de opciones de `state`, en orden.
Listed OptionsIn(const ItemSetState& state)
{
    Listed listed;
    const std::size_t total = std::size_t{state.list.count9348} + state.list.count9349;
    for (std::size_t index = 0; index < total; ++index)
    {
        EXPECT_EQ(state.list.values[index], state.list.secondValues[index]);
        listed.push_back({.option = state.list.entries[index][0], .value = state.list.values[index]});
    }
    return listed;
}

// 0x40cfa0: el byte +0x1c & 3 elige el byte 0, 1 o 2 del registro del tipo; 0 no cuenta, y los otros
// bits del +0x1c no importan. La pieza guarda el conjunto, la cuenta y la clase de la última.
TEST(OwnItemSetsTest, APieceIsTheSetOfItsTypeByTheLowBitsOfByte1c)
{
    SetTypes types;
    types.Set(0x40, 0, 5).Set(0x40, 1, 7).Set(0x40, 2, 9).Set(0x40, 3, 11);
    const ItemSetTables tables = TablesOf(types, SetOptions{});
    ItemSetPieces pieces{};
    for (const std::uint8_t flags1c : {std::uint8_t{0x00}, std::uint8_t{0xfc}, std::uint8_t{0xfd}, std::uint8_t{0x02},
                                       std::uint8_t{0x03}, std::uint8_t{0x01}})
    {
        ASSERT_TRUE(Client::Scenes::AddItemSetPiece(pieces, tables.types, 0x40, flags1c).HasValue());
    }
    EXPECT_EQ(pieces[0].setId, 5);
    EXPECT_EQ(pieces[0].count, 2);
    EXPECT_EQ(pieces[0].kind, 0);
    EXPECT_EQ(pieces[1].setId, 7);
    EXPECT_EQ(pieces[1].count, 1);
    EXPECT_EQ(pieces[1].kind, 1);
    EXPECT_EQ(pieces[2].setId, 9);
    EXPECT_EQ(pieces[2].count, 1);
    EXPECT_EQ(pieces[2].kind, 2);
    // El byte 3 no lo lee nadie.
    EXPECT_EQ(pieces[3].setId, 0);
}

// 0 y 0xff no son un conjunto; el mismo conjunto suma a su lugar y se queda con la clase de la última
// pieza; con los cuatro lugares ocupados por otros, la pieza no cuenta. El tipo 8.191 está en la tabla y
// el 8.192 no (Q-C187).
TEST(OwnItemSetsTest, PiecesFillFourSetsAndATypeOutsideIsAnError)
{
    SetTypes types;
    types.Set(1, 0, 0).Set(2, 0, 3).Set(2, 1, 3).Set(3, 0, 4).Set(4, 0, 6).Set(5, 0, 8).Set(6, 0, 10);
    types.Set(0x1fff, 0, 12);
    const ItemSetTables tables = TablesOf(types, SetOptions{});
    struct Worn
    {
        std::size_t type;
        std::uint8_t flags1c;
    };
    ItemSetPieces pieces{};
    for (const Worn& worn :
         {Worn{1, 1}, Worn{7, 1}, Worn{2, 1}, Worn{2, 2}, Worn{3, 1}, Worn{4, 1}, Worn{5, 1}, Worn{6, 1}})
    {
        ASSERT_TRUE(Client::Scenes::AddItemSetPiece(pieces, tables.types, worn.type, worn.flags1c).HasValue());
    }
    const ItemSetPieces expected = {ItemSetPiece{.setId = 3, .count = 2, .kind = 1}, ItemSetPiece{4, 1, 0},
                                    ItemSetPiece{6, 1, 0}, ItemSetPiece{8, 1, 0}};
    for (std::size_t slot = 0; slot < pieces.size(); ++slot)
    {
        SCOPED_TRACE(slot);
        EXPECT_EQ(pieces[slot].setId, expected[slot].setId);
        EXPECT_EQ(pieces[slot].count, expected[slot].count);
        EXPECT_EQ(pieces[slot].kind, expected[slot].kind);
    }

    ItemSetPieces last{};
    ASSERT_TRUE(Client::Scenes::AddItemSetPiece(last, tables.types, 0x1fff, 1).HasValue());
    EXPECT_EQ(last[0].setId, 12);
    const auto outside = Client::Scenes::AddItemSetPiece(last, tables.types, 0x2000, 1);
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("Q-C187"), std::string::npos);
    EXPECT_NE(outside.Error().find("8192"), std::string::npos);
    // Sin conjunto (& 3 en 0) no lee la tabla.
    EXPECT_TRUE(Client::Scenes::AddItemSetPiece(last, tables.types, 0x2000, 4).HasValue());
}

// 0x40d010 con n = piezas − 1: los primeros min(n, 6) pares, con las dos opciones de cada uno y sin las
// 0xff; el valor va a los dos DWORD. Con una pieza el conjunto no aplica. Las opciones de 0x18 en adelante
// piden una marca de la clase que estos conjuntos no tienen. Con +0x3c en 0xff nunca está completo.
TEST(OwnItemSetsTest, EachPieceAfterTheFirstAddsAPairOfOptions)
{
    SetOptions options;
    options.Named(5, "Cinco", 0xff)
        .Pair(5, 0, 0, 10, 1, 11)
        .Pair(5, 1, 2, 12)
        .Pair(5, 2, 0x18, 13, 3, 14)
        .Pair(5, 3, 4, 15)
        .Pair(5, 4, 0x17, 16)
        .Pair(5, 5, 0x20, 17, 0x05, 18);
    const ItemSetTables tables = TablesOf(SetTypes{}, options);
    ActorState hero;
    struct Case
    {
        std::uint8_t pieces;
        Listed listed;
    };
    for (const Case& expected : {Case{1, {}}, Case{2, {{0, 10}, {1, 11}}}, Case{3, {{0, 10}, {1, 11}, {2, 12}}},
                                 Case{4, {{0, 10}, {1, 11}, {2, 12}, {3, 14}}},
                                 Case{8, {{0, 10}, {1, 11}, {2, 12}, {3, 14}, {4, 15}, {0x17, 16}, {5, 18}}}})
    {
        SCOPED_TRACE(int{expected.pieces});
        ItemSetState state;
        ASSERT_TRUE(
            Client::Scenes::ApplyItemSetOptions(state, OnePiece(5, expected.pieces), tables.options, hero).HasValue());
        EXPECT_EQ(OptionsIn(state), expected.listed);
        EXPECT_EQ(state.list.count9349, 0);
        EXPECT_EQ(hero.byte358, 0);
    }
}

// 0x40d278..0x40d295: después de los seis pares, mientras i < n − 6, una opción de +0x38 + i con su valor
// en +0x3a + i. Pide n de 13 en adelante, que 0x40e1b0 no da (cuenta hasta 10 piezas). Con n de 25 en
// adelante el valor cae después del registro: error (Q-C187).
TEST(OwnItemSetsTest, TheExtraOptionsOnlyComeWithMoreThanThirteenPieces)
{
    SetOptions options;
    options.Named(5, "Cinco", 0xff).Byte(5, 0x3e, 7).Byte(5, 0x40, 9);
    const ItemSetTables tables = TablesOf(SetTypes{}, options);
    ActorState hero;
    ItemSetState state;

    ASSERT_TRUE(Client::Scenes::ApplyItemSetOptions(state, OnePiece(5, 13), tables.options, hero).HasValue());
    EXPECT_EQ(OptionsIn(state), Listed{});
    ASSERT_TRUE(Client::Scenes::ApplyItemSetOptions(state, OnePiece(5, 14), tables.options, hero).HasValue());
    EXPECT_EQ(OptionsIn(state), (Listed{{7, 9}}));

    const auto outside = Client::Scenes::ApplyItemSetOptions(state, OnePiece(5, 31), tables.options, hero);
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("31 piezas"), std::string::npos);
    EXPECT_NE(outside.Error().find("Q-C187"), std::string::npos);
}

// 0x40d35e..0x40d457: completo con piezas − 1 ≥ +0x3c − 2 (+0x3c es las piezas + 1), suma las opciones del
// completo y pone el +0x358 del héroe publicado en 1. Con una menos, no.
TEST(OwnItemSetsTest, AFullSetAddsItsOptionsAndMarksThePublishedHero)
{
    SetOptions options;
    options.Named(3, "Tres", 4).Pair(3, 0, 2, 15).Pair(3, 1, 1, 15).Full(3, 0, 0x13, 20).Full(3, 1, 0x0c, 30);
    const ItemSetTables tables = TablesOf(SetTypes{}, options);
    ActorState hero;
    hero.byte358 = 7;
    ItemSetState state;

    ASSERT_TRUE(Client::Scenes::ApplyItemSetOptions(state, OnePiece(3, 2), tables.options, hero).HasValue());
    EXPECT_EQ(OptionsIn(state), (Listed{{2, 15}}));
    EXPECT_EQ(hero.byte358, 0);

    ASSERT_TRUE(Client::Scenes::ApplyItemSetOptions(state, OnePiece(3, 3), tables.options, hero).HasValue());
    EXPECT_EQ(OptionsIn(state), (Listed{{2, 15}, {1, 15}, {0x13, 20}, {0x0c, 30}}));
    EXPECT_EQ(hero.byte358, 1);
    EXPECT_EQ(NameIn(state.names[0]), "Tres");
    EXPECT_EQ(state.list.setIds[0], 3);
    EXPECT_EQ(state.byte9307, 0);

    // +0x3c en 0: −2, completo desde dos piezas.
    options.Byte(3, 0x3c, 0);
    const ItemSetTables anyCount = TablesOf(SetTypes{}, options);
    ASSERT_TRUE(Client::Scenes::ApplyItemSetOptions(state, OnePiece(3, 2), anyCount.options, hero).HasValue());
    EXPECT_EQ(OptionsIn(state), (Listed{{2, 15}, {0x13, 20}, {0x0c, 30}}));
    EXPECT_EQ(hero.byte358, 1);
}

// 0x40d0d0..0x40d14d y 0x40d29c..0x40d2bd, en todas las clases (+0x1d4 & 7) con y sin evolución (>> 3):
// la marca de la clase es rec[0x47 + clase]. Una opción de 0x18 en adelante entra con la marca distinta de
// 0 y la evolución ≥ marca − 1. La bandera de la lista es 1 en las clases 0 a 2 con la marca en 1, o en 2
// con evolución, y en la 3 solo con 1; la 4 nunca. Las clases 5 a 7 leen la marca en el registro
// siguiente.
TEST(OwnItemSetsTest, ClassMarksGateTheHighOptionsAndSetTheFlag)
{
    struct Case
    {
        std::uint8_t heroClass;
        std::uint8_t mark;
        bool allowed;
        std::uint8_t flag;
    };
    const std::initializer_list<Case> cases = {
        {0x00, 1, true, 1}, {0x00, 2, false, 0}, {0x08, 2, true, 1},  {0x08, 3, false, 0},
        {0x10, 3, true, 0}, {0x01, 1, true, 1},  {0x09, 2, true, 1},  {0x02, 1, true, 1},
        {0x0a, 2, true, 1}, {0x03, 1, true, 1},  {0x0b, 2, true, 0},  {0x04, 1, true, 0},
        {0x0c, 2, true, 0}, {0x04, 0, false, 0}, {0x00, 0, false, 0}, {0x1b, 0xff, false, 0}};
    for (const Case& expected : cases)
    {
        SCOPED_TRACE(testing::Message() << "clase " << int{expected.heroClass} << ", marca " << int{expected.mark});
        SetOptions options;
        options.Named(9, "Nueve", 0xff).Pair(9, 0, 0x30, 40, 4, 41);
        options.Byte(9, static_cast<std::size_t>(0x47 + (expected.heroClass & 7)), expected.mark);
        const ItemSetTables tables = TablesOf(SetTypes{}, options);
        ActorState hero;
        hero.characterClass = expected.heroClass;
        ItemSetState state;
        ASSERT_TRUE(Client::Scenes::ApplyItemSetOptions(state, OnePiece(9, 2), tables.options, hero).HasValue());
        EXPECT_EQ(OptionsIn(state), (expected.allowed ? Listed{{0x30, 40}, {4, 41}} : Listed{{4, 41}}));
        EXPECT_EQ(state.list.entries[0][1], expected.flag);
    }

    // Las clases 5 a 7 caen en los bytes 0 a 2 del registro siguiente, que acá son 1, 32 y 0: con la
    // evolución en 31, la 5 y la 6 (31 ≥ 32 − 1) entran y la 7 no.
    for (const std::uint8_t heroClass : {std::uint8_t{5}, std::uint8_t{6}, std::uint8_t{7}})
    {
        SCOPED_TRACE(int{heroClass});
        SetOptions options;
        options.Named(9, "Nueve", 0xff).Pair(9, 0, 0x30, 40);
        options.Byte(10, 0, 1).Byte(10, 1, 32).Byte(10, 2, 0);
        const ItemSetTables tables = TablesOf(SetTypes{}, options);
        ActorState hero;
        hero.characterClass = static_cast<std::uint8_t>(heroClass | (0x1f << 3));
        ItemSetState state;
        ASSERT_TRUE(Client::Scenes::ApplyItemSetOptions(state, OnePiece(9, 2), tables.options, hero).HasValue());
        EXPECT_EQ(OptionsIn(state), (heroClass == 7 ? Listed{} : Listed{{0x30, 40}}));
        if (heroClass != 7)
        {
            EXPECT_EQ(state.list.entries[0][1], 0);
        }
    }
}

// Después del último conjunto, la marca de la clase 5 cae fuera de la tabla (Q-C187). Una opción de 0x18
// en adelante la lee; las de antes, no.
TEST(OwnItemSetsTest, TheMarkPastTheLastSetIsAnError)
{
    SetOptions options;
    options.Named(63, "Ultimo", 0xff).Pair(63, 0, 1, 5).Pair(63, 1, 0x18, 6);
    const ItemSetTables tables = TablesOf(SetTypes{}, options);
    ActorState hero;
    hero.characterClass = 5;
    ItemSetState state;
    ASSERT_TRUE(Client::Scenes::ApplyItemSetOptions(state, OnePiece(63, 2), tables.options, hero).HasValue());
    const auto outside = Client::Scenes::ApplyItemSetOptions(state, OnePiece(63, 3), tables.options, hero);
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("Q-C187"), std::string::npos);
    EXPECT_EQ(state.list.count9348, 0);
}

// 0x40d16d..0x40d233: el nombre va a su clase de pieza. Otro conjunto de la misma clase va al primer
// nombre vacío o al segundo, y deja +0x9307 en sus piezas − 1; las opciones del completo le suman 1 a
// +0x9307 si no es 0. Las opciones de la clase 0 van primero y las de la 1 detrás.
TEST(OwnItemSetsTest, NamesGoToTheirKindAndAnotherSetToTheSecondName)
{
    SetOptions options;
    options.Named(1, "Uno", 3).Pair(1, 0, 0, 1).Full(1, 0, 1, 2);
    options.Named(2, "Dos", 3).Pair(2, 0, 2, 3).Pair(2, 1, 3, 4).Full(2, 0, 4, 5).Full(2, 1, 0, 6);
    options.Named(4, "Cuatro", 0xff).Pair(4, 0, 4, 7);
    const ItemSetTables tables = TablesOf(SetTypes{}, options);
    ActorState hero;
    ItemSetState state;
    const ItemSetPieces pieces = {ItemSetPiece{.setId = 4, .count = 2, .kind = 1}, ItemSetPiece{1, 2, 0},
                                  ItemSetPiece{2, 3, 0}, ItemSetPiece{}};

    ASSERT_TRUE(Client::Scenes::ApplyItemSetOptions(state, pieces, tables.options, hero).HasValue());

    // "Cuatro" en el de la clase 1; "Uno" en el de la 0; "Dos", de la 0 con otro nombre ahí, al segundo.
    EXPECT_EQ(NameIn(state.names[0]), "Uno");
    EXPECT_EQ(NameIn(state.names[1]), "Dos");
    // strcpy copia el nombre con su 0 sobre "Cuatro" y deja lo de después: "Dos\0ro\0".
    EXPECT_EQ(std::string(state.names[1].data() + 4), "ro");
    EXPECT_EQ(state.list.setIds[0], 2);
    EXPECT_EQ(state.list.setIds[1], 4);
    // +0x9307: 2 al entrar "Dos" y + 1 por cada una de sus dos opciones del completo.
    EXPECT_EQ(state.byte9307, 4);
    // De la clase 0, Uno con un par y una del completo, y Dos con dos pares y dos del completo.
    EXPECT_EQ(state.list.count9348, 6);
    EXPECT_EQ(state.list.count9349, 1);
    EXPECT_EQ(OptionsIn(state), (Listed{{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {0, 6}, {4, 7}}));
    EXPECT_EQ(hero.byte358, 1);

    // El mismo conjunto otra vez se queda en su nombre y no toca +0x9307.
    const ItemSetPieces again = {ItemSetPiece{.setId = 1, .count = 2, .kind = 0}, ItemSetPiece{1, 2, 0}, ItemSetPiece{},
                                 ItemSetPiece{}};
    ASSERT_TRUE(Client::Scenes::ApplyItemSetOptions(state, again, tables.options, hero).HasValue());
    EXPECT_EQ(NameIn(state.names[0]), "Uno");
    EXPECT_EQ(NameIn(state.names[1]), "");
    EXPECT_EQ(state.byte9307, 0);
}

// Lo que el original lee o escribe fuera de sus tablas y listas es un error (Q-C187): un conjunto de 64
// o más, la clase de pieza 2 (el & 3 en 3), un nombre sin 0 en sus 0x20 bytes, más de 16 opciones de una
// clase o más de 16 entre las dos. Las cuentas quedan en 0, como las deja 0x40d010 al entrar.
TEST(OwnItemSetsTest, WhatFallsOutsideTheTablesAndListsIsAnError)
{
    SetOptions options;
    options.Named(1, "Uno", 2);
    options.Byte(2, 0, 'A');
    for (std::size_t set : {std::size_t{3}, std::size_t{4}})
    {
        options.Named(set, "Muchas", 2);
        for (std::size_t pair = 0; pair < 6; ++pair)
        {
            options.Pair(set, pair, 0, 1, 1, 1);
        }
        for (std::size_t full = 0; full < 5; ++full)
        {
            options.Full(set, full, 2, 1);
        }
    }
    const ItemSetTables tables = TablesOf(SetTypes{}, options);
    ActorState hero;
    struct Case
    {
        ItemSetPieces pieces;
        std::string_view shown;
    };
    // Con cuatro piezas, 6 opciones de los pares y 5 del completo por conjunto: 11 + 11.
    const ItemSetPieces twoKinds = {ItemSetPiece{3, 4, 0}, ItemSetPiece{4, 4, 1}, ItemSetPiece{}, ItemSetPiece{}};
    for (const Case& outside :
         {Case{OnePiece(64, 2), "conjunto 64"}, Case{OnePiece(1, 2, 2), "clase de pieza 2"},
          Case{OnePiece(2, 2), "no termina"}, Case{OnePiece(3, 7), "más de 16"}, Case{twoKinds, "no entran"}})
    {
        SCOPED_TRACE(outside.shown);
        ItemSetState state;
        state.list.count9348 = 9;
        state.list.count9349 = 9;
        const auto applied = Client::Scenes::ApplyItemSetOptions(state, outside.pieces, tables.options, hero);
        ASSERT_FALSE(applied.HasValue());
        EXPECT_NE(applied.Error().find("Q-C187"), std::string::npos);
        EXPECT_NE(applied.Error().find(outside.shown), std::string::npos);
        EXPECT_EQ(state.list.count9348, 0);
        EXPECT_EQ(state.list.count9349, 0);
    }
}

// 0x550d29..0x550d3f: las marcas de +0x937c en 0, y nada más de la lista. Lo que 0x40da20 (el código de
// 0x40d690) ya sumó, después de la limpieza se puede sumar otra vez.
TEST(OwnItemSetsTest, ClearingTheMarksLetsTheOptionsAddAgain)
{
    ItemSetState state;
    state.list.count9348 = 2;
    state.list.entries[0] = {1, 1, 0};
    state.list.entries[1] = {1, 0, 0};
    state.list.values[0] = 10;
    state.list.values[1] = 20;
    std::uint16_t word = 0;
    ASSERT_TRUE(Client::Scenes::AddItemSetOptionValues(state.list, word, 0x8d).HasValue());
    ASSERT_TRUE(Client::Scenes::AddItemSetOptionValues(state.list, word, 0x8d).HasValue());
    EXPECT_EQ(word, 30);
    EXPECT_EQ(state.list.used, (std::array<std::uint8_t, 16>{1, 1}));

    Client::Scenes::ClearItemSetOptionMarks(state.list);

    EXPECT_EQ(state.list.used, (std::array<std::uint8_t, 16>{}));
    EXPECT_EQ(state.list.count9348, 2);
    EXPECT_EQ(state.list.entries[0][1], 1);
    EXPECT_EQ(state.list.values[1], 20);
    ASSERT_TRUE(Client::Scenes::AddItemSetOptionValues(state.list, word, 0x8d).HasValue());
    EXPECT_EQ(word, 60);
}

// Un registro propio con lo que lee 0x40e1b0: todos los lugares vacíos.
OwnCharacterRecord Unequipped()
{
    OwnCharacterRecord own;
    for (ItemState& item : own.equipment)
    {
        item.id = EmptyType;
    }
    return own;
}

ItemState Piece(std::uint16_t type, std::uint8_t setIndex)
{
    ItemState item;
    item.id = type;
    item.flags1c = setIndex;
    return item;
}

// Lo que pide 0x40e1b0, con la cuenta de accesos y el rand() del fixture.
class OwnItemSetsFixture : public ::testing::Test
{
protected:
    Client::Scenes::OwnStatsContext Context(const ItemSetTables* tables)
    {
        return {.itemSetTables = tables,
                .itemSets = &m_state,
                .items = &m_items,
                .publishedHero = m_hero,
                .ownRecordAccesses = m_accesses,
                .random = m_random};
    }

    ItemSetState m_state;
    Client::Data::Tables::ItemTable m_items = Client::Tests::ItemsMarking({});
    ActorState m_hero;
    std::uint8_t m_accesses = 1;
    CrtRandom m_random{3};
};

// 0x40de10: los doce lugares (también las alas y la mascota) con durabilidad, el tipo 1 a la fuerza, el
// 2 a la agilidad, el 3 a la energía y el 4 a la vitalidad, 5 · valor; después las opciones 0 a 3 de la
// lista, que quedan marcadas. Abre y cierra el registro en cada lugar: con la cuenta en 0, 12 cierres.
TEST_F(OwnItemSetsFixture, StatCopiesTakeTheItemBonusesAndTheSetOptions)
{
    // El byte +0x44 de Item.bmd: el tipo 1 en 1, el 2 en 2, el 3 en 3 y el 4 en 4.
    std::vector<std::byte> plain(Client::Data::Tables::ItemTableSize);
    for (std::size_t type = 1; type <= 4; ++type)
    {
        plain[type * Client::Data::Tables::ItemRecordSize + 0x44] = static_cast<std::byte>(type);
    }
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Data::Tables::ItemRecordSize);
    m_items = Client::Data::Tables::ItemTable(std::span<const std::byte>(encrypted));
    OwnCharacterRecord own = Unequipped();
    struct Worn
    {
        std::size_t slot;
        std::uint16_t type;
        std::uint8_t value;
    };
    for (const Worn& worn : {Worn{7, 1, 3}, Worn{8, 2, 2}, Worn{0, 3, 1}, Worn{11, 4, 3}, Worn{2, 1, 1}})
    {
        ItemState& item = own.equipment[worn.slot];
        item.id = worn.type;
        item.durability = 1;
        item.flags1c = static_cast<std::uint8_t>(worn.value << 2);
    }
    // Un lugar sin durabilidad no lee Item.bmd, aunque el tipo esté fuera.
    own.equipment[5].id = EmptyType;
    m_state.list.count9348 = 4;
    for (std::size_t index = 0; index < 4; ++index)
    {
        m_state.list.entries[index][0] = static_cast<std::uint8_t>(index);
        m_state.list.values[index] = static_cast<std::int32_t>(100 * (index + 1));
    }
    m_accesses = 0;
    OwnStatCopies copies{.strength = 1, .agility = 2, .vitality = 3, .energy = 4};

    ASSERT_TRUE(Client::Scenes::AddOwnBonusesToStatCopies(copies, own, Context(nullptr)).HasValue());

    EXPECT_EQ(copies.strength, 1 + 15 + 5 + 100);
    EXPECT_EQ(copies.agility, 2 + 10 + 200);
    EXPECT_EQ(copies.energy, 4 + 5 + 300);
    EXPECT_EQ(copies.vitality, 3 + 15 + 400);
    EXPECT_EQ(m_state.list.used, (std::array<std::uint8_t, 16>{1, 1, 1, 1}));
    EXPECT_EQ(m_accesses, 0);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(3, 12 * OwnRecordCloseRandoms).State());
}

// Q-C184 desde 0x40de10: un lugar vaciado con durabilidad corta, con el acceso cerrado.
TEST_F(OwnItemSetsFixture, AnEmptySlotWithDurabilityStopsTheStatCopies)
{
    OwnCharacterRecord own = Unequipped();
    own.equipment[4].durability = 1;
    OwnStatCopies copies;
    const auto added = Client::Scenes::AddOwnBonusesToStatCopies(copies, own, Context(nullptr));
    ASSERT_FALSE(added.HasValue());
    EXPECT_NE(added.Error().find("0x40de10, lugar 4"), std::string::npos);
    EXPECT_NE(added.Error().find("Q-C184"), std::string::npos);
    EXPECT_EQ(m_accesses, 1);
}

// Sin las tablas o sin el estado de [0x6eed3c], 0x40e1b0 corta antes de tocar nada: ni la lista, ni el
// héroe, ni un acceso.
TEST_F(OwnItemSetsFixture, WithoutTheTablesNothingIsTouched)
{
    m_state.list.entries[0][0] = 0x42;
    m_hero.byte358 = 3;
    m_accesses = 0;
    const auto recalculated = Client::Scenes::RecalculateOwnItemSets(Unequipped(), Context(nullptr));
    ASSERT_FALSE(recalculated.HasValue());
    EXPECT_NE(recalculated.Error().find("0x40c600"), std::string::npos);
    EXPECT_EQ(m_state.list.entries[0][0], 0x42);
    EXPECT_EQ(m_hero.byte358, 3);
    EXPECT_EQ(m_random.State(), CrtRandom(3).State());

    // Con las tablas y sin dónde escribir, igual.
    const ItemSetTables tables = TablesOf(SetTypes{}, SetOptions{});
    Client::Scenes::OwnStatsContext withoutState = Context(&tables);
    withoutState.itemSets = nullptr;
    const auto stateless = Client::Scenes::RecalculateOwnItemSets(Unequipped(), withoutState);
    ASSERT_FALSE(stateless.HasValue());
    EXPECT_NE(stateless.Error().find("WorldState::ItemSets"), std::string::npos);
    EXPECT_EQ(m_hero.byte358, 3);
    EXPECT_EQ(m_random.State(), CrtRandom(3).State());
}

// 0x40e1b0 entero, con tres conjuntos: las alas y la mascota no cuentan; el mismo ítem en las dos manos
// o en los dos anillos cuenta una vez, y con otro conjunto, dos. Abre y cierra el registro 10 + 1 + 12 +
// 10 veces: con la cuenta en 0, 33 cierres de 3.848 rand().
TEST_F(OwnItemSetsFixture, TheTwoPassesCountEachPieceOnce)
{
    SetTypes types;
    types.Set(0x10, 0, 1).Set(0x11, 0, 1).Set(0x20, 0, 2).Set(0x20, 1, 2).Set(0x30, 0, 3);
    SetOptions options;
    options.Named(1, "Uno", 0xff).Pair(1, 0, 0, 1).Pair(1, 1, 1, 2).Pair(1, 2, 2, 3);
    options.Named(2, "Dos", 0xff).Pair(2, 0, 3, 4).Pair(2, 1, 4, 5);
    options.Named(3, "Tres", 0xff).Pair(3, 0, 0x10, 6);
    const ItemSetTables tables = TablesOf(types, options);
    OwnCharacterRecord own = Unequipped();
    own.equipment[0] = Piece(0x10, 1);
    own.equipment[1] = Piece(0x10, 1); // el mismo que el 0: no cuenta
    own.equipment[2] = Piece(0x11, 1);
    own.equipment[7] = Piece(0x11, 1); // las alas no cuentan
    own.equipment[8] = Piece(0x30, 1); // la mascota tampoco
    own.equipment[9] = Piece(0x30, 1);
    own.equipment[10] = Piece(0x20, 1);
    own.equipment[11] = Piece(0x20, 2); // otro conjunto del mismo tipo: cuenta
    m_accesses = 0;

    ASSERT_TRUE(Client::Scenes::RecalculateOwnItemSets(own, Context(&tables)).HasValue());

    // Uno con dos piezas (0x10 y 0x11); Dos con dos (la de la clase 1 al final); Tres con una, sin opciones.
    EXPECT_EQ(OptionsIn(m_state), (Listed{{0, 1}, {3, 4}}));
    EXPECT_EQ(m_state.list.count9348, 1);
    EXPECT_EQ(m_state.list.count9349, 1);
    EXPECT_EQ(NameIn(m_state.names[0]), "Uno");
    EXPECT_EQ(NameIn(m_state.names[1]), "Dos");
    EXPECT_EQ(m_hero.byte358, 0);
    EXPECT_EQ(m_accesses, 0);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(3, 33 * OwnRecordCloseRandoms).State());
}

// La segunda pasada salta los ítems cuyos requisitos (+0x1e la fuerza, +0x20 la agilidad, +0x22 la
// energía; sin signo) pasan las copias, que ya llevan lo de la primera: una pieza que solo entra gracias a
// la opción de su propio conjunto cuenta. Después del 0xff de 0x40e4bf, la lista es la de la segunda.
TEST_F(OwnItemSetsFixture, TheSecondPassChecksRequirementsAgainstTheBoostedCopies)
{
    SetTypes types;
    types.Set(0x10, 0, 1).Set(0x11, 0, 1).Set(0x12, 0, 1);
    SetOptions options;
    // Con dos piezas, +10 a la agilidad (la opción 1, 0x8d); con tres, +10 a la energía (la 2).
    options.Named(1, "Uno", 0xff).Pair(1, 0, 1, 10).Pair(1, 1, 2, 10);
    const ItemSetTables tables = TablesOf(types, options);
    OwnCharacterRecord own = Unequipped();
    own.strength = 50;
    own.agility = 20;
    own.energy = 40;
    own.equipment[2] = Piece(0x10, 1);
    own.equipment[3] = Piece(0x11, 1);
    own.equipment[3].word20 = 30; // entra con los 10 de la opción 1
    own.equipment[4] = Piece(0x12, 1);
    own.equipment[4].word22 = 51; // no entra: la energía llega a 50 en la primera pasada

    ASSERT_TRUE(Client::Scenes::RecalculateOwnItemSets(own, Context(&tables)).HasValue());

    EXPECT_EQ(OptionsIn(m_state), (Listed{{1, 10}}));
    // De la primera pasada quedan las marcas de 0x40de10; el 0xff de la segunda, en lo que no escribió.
    EXPECT_EQ(m_state.list.used[0], 1);
    EXPECT_EQ(m_state.list.used[1], 1);
    EXPECT_EQ(m_state.list.entries[0][2], 0xff);
    EXPECT_EQ(m_state.list.entries[1][0], 0xff);
    EXPECT_EQ(m_state.list.entries[5][0], 0xff);
    EXPECT_EQ(m_state.list.entries[5][1], 0);
}

// -0x7c y -0x60 pasan de una pasada a la otra: si el lugar 0 no da sus requisitos en la segunda, el 1 se
// compara con el 10 de la primera, y el mismo ítem en el 1 y en el 10 cuenta una vez en lugar de dos.
TEST_F(OwnItemSetsFixture, ThePairCheckKeepsTheFirstPassRingForTheHands)
{
    SetTypes types;
    types.Set(0x10, 0, 1).Set(0x20, 0, 2);
    SetOptions options;
    options.Named(1, "Uno", 0xff).Pair(1, 0, 0, 1).Pair(1, 1, 1, 2);
    options.Named(2, "Dos", 0xff).Pair(2, 0, 3, 4);
    const ItemSetTables tables = TablesOf(types, options);
    OwnCharacterRecord own = Unequipped();
    own.equipment[0] = Piece(0x20, 1);
    // La fuerza está en 0 y la copia llega a 1 con la opción 0 de Uno: no entra en la segunda pasada.
    own.equipment[0].word1e = 2;
    own.equipment[1] = Piece(0x10, 1);
    own.equipment[9] = Piece(0x20, 1);
    own.equipment[10] = Piece(0x10, 1);
    own.equipment[11] = Piece(0x10, 1);

    ASSERT_TRUE(Client::Scenes::RecalculateOwnItemSets(own, Context(&tables)).HasValue());

    // Primera pasada: Uno con 2 (el 1 y el 10; el 11 repite el 10) y Dos con 2 (el 0 y el 9). Segunda:
    // el 1 repite el 10 de la primera, así que Uno queda con 1 (el 10), y Dos con 1 (el 9).
    EXPECT_EQ(OptionsIn(m_state), Listed{});
    EXPECT_EQ(m_state.list.setIds[0], 0);
}
} // namespace
