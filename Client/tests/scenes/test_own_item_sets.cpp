#include "Player/OwnItemSets.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "Player/ItemSetOptionList.h"
#include "Player/ItemSetState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Player/OwnDerivedStats.h"
#include "Kernel/ItemState.h"
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

// Client::Player: 0x40e1b0 and what it calls, 0x40cfa0, 0x40d010, 0x40de10 and 0x40da20 (06, "Los stats del registro
// propio"; 07, Q-C184 and Q-C187). The tables are synthetic; the values come from the ASM checked against the PE
// c32bbd5f and the rand() calls from the CRT's model, none from a run of the original.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::ItemState;
using Client::Kernel::OwnCharacterRecord;
using Client::Models::OwnRecordCloseRandoms;
using Client::Player::ItemSetPiece;
using Client::Player::ItemSetPieces;
using Client::Player::ItemSetState;
using Client::Player::OwnStatCopies;
using Client::Tables::ItemSetOptionRecordSize;
using Client::Tables::ItemSetOptionTableSize;
using Client::Tables::ItemSetTables;
using Client::Tables::ItemSetTypeRecordSize;
using Client::Tables::ItemSetTypeTableSize;

constexpr std::uint8_t None = 0xff;
constexpr std::uint16_t EmptyType = 0xffff;

// A plain ItemSetType.bmd: everything at 0xff, like the real file's types without a set.
class SetTypes
{
public:
    SetTypes() : m_plain(ItemSetTypeTableSize, std::byte{None}) {}

    // Byte `index` (0 to 3) of `type`'s record.
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

// A plain ItemSetOption.bmd: the unused sets at 0xff, as in the real file.
class SetOptions
{
public:
    SetOptions() : m_plain(ItemSetOptionTableSize, std::byte{None}) {}

    // A named set, +0x3c at `fullPieces` and the class marks at 0, like the real ones.
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

    // Pair `pair` (0 to 5): its two options and its two values.
    SetOptions& Pair(std::size_t set, std::size_t pair, std::uint8_t first, std::uint8_t firstValue,
                     std::uint8_t second = None, std::uint8_t secondValue = None)
    {
        At(set, 0x20 + 2 * pair) = std::byte{first};
        At(set, 0x21 + 2 * pair) = std::byte{second};
        At(set, 0x2c + 2 * pair) = std::byte{firstValue};
        At(set, 0x2d + 2 * pair) = std::byte{secondValue};
        return *this;
    }

    // Option `index` (0 to 4) of the complete set.
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

std::string NameIn(const std::array<char, Client::Player::ItemSetNameSize>& slot)
{
    return std::string(slot.data());
}

ItemSetPieces OnePiece(std::uint8_t setId, std::uint8_t count, std::uint8_t kind = 0)
{
    ItemSetPieces pieces{};
    pieces[0] = {.setId = setId, .count = count, .kind = kind};
    return pieces;
}

// A list option and its value. An aggregate and not std::pair: pair's constructor from int narrows inside <utility>
// (C4242 on MSVC).
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

// `state`'s option list, in order.
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

// 0x40cfa0: byte +0x1c & 3 picks byte 0, 1 or 2 of the type's record; 0 does not count, and +0x1c's other bits do not
// matter. The piece keeps the set, the count and the last one's kind.
TEST(OwnItemSetsTest, APieceIsTheSetOfItsTypeByTheLowBitsOfByte1c)
{
    SetTypes types;
    types.Set(0x40, 0, 5).Set(0x40, 1, 7).Set(0x40, 2, 9).Set(0x40, 3, 11);
    const ItemSetTables tables = TablesOf(types, SetOptions{});
    ItemSetPieces pieces{};
    for (const std::uint8_t flags1c : {std::uint8_t{0x00}, std::uint8_t{0xfc}, std::uint8_t{0xfd}, std::uint8_t{0x02},
                                       std::uint8_t{0x03}, std::uint8_t{0x01}})
    {
        ASSERT_TRUE(Client::Player::AddItemSetPiece(pieces, tables.types, 0x40, flags1c).HasValue());
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
    // Nobody reads byte 3.
    EXPECT_EQ(pieces[3].setId, 0);
}

// 0 and 0xff are not a set; the same set adds to its place and keeps the last piece's kind; with the four places taken
// by others, the piece does not count. Type 8,191 is in the table and 8,192 is not (Q-C187).
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
        ASSERT_TRUE(Client::Player::AddItemSetPiece(pieces, tables.types, worn.type, worn.flags1c).HasValue());
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
    ASSERT_TRUE(Client::Player::AddItemSetPiece(last, tables.types, 0x1fff, 1).HasValue());
    EXPECT_EQ(last[0].setId, 12);
    const auto outside = Client::Player::AddItemSetPiece(last, tables.types, 0x2000, 1);
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("Q-C187"), std::string::npos);
    EXPECT_NE(outside.Error().find("8192"), std::string::npos);
    // Without a set (& 3 at 0) it does not read the table.
    EXPECT_TRUE(Client::Player::AddItemSetPiece(last, tables.types, 0x2000, 4).HasValue());
}

// 0x40d010 with n = pieces − 1: the first min(n, 6) pairs, with each one's two options and without the 0xff ones; the
// value goes to both DWORDs. With one piece the set does not apply. Options from 0x18 on ask for a class mark these
// sets do not have. With +0x3c at 0xff it is never complete.
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
            Client::Player::ApplyItemSetOptions(state, OnePiece(5, expected.pieces), tables.options, hero).HasValue());
        EXPECT_EQ(OptionsIn(state), expected.listed);
        EXPECT_EQ(state.list.count9349, 0);
        EXPECT_EQ(hero.byte358, 0);
    }
}

// 0x40d278..0x40d295: after the six pairs, while i < n − 6, an option of +0x38 + i with its value at +0x3a + i. It
// needs n from 13 on, which 0x40e1b0 does not give (it counts up to 10 pieces). With n from 25 on the value falls past
// the record: an error (Q-C187).
TEST(OwnItemSetsTest, TheExtraOptionsOnlyComeWithMoreThanThirteenPieces)
{
    SetOptions options;
    options.Named(5, "Cinco", 0xff).Byte(5, 0x3e, 7).Byte(5, 0x40, 9);
    const ItemSetTables tables = TablesOf(SetTypes{}, options);
    ActorState hero;
    ItemSetState state;

    ASSERT_TRUE(Client::Player::ApplyItemSetOptions(state, OnePiece(5, 13), tables.options, hero).HasValue());
    EXPECT_EQ(OptionsIn(state), Listed{});
    ASSERT_TRUE(Client::Player::ApplyItemSetOptions(state, OnePiece(5, 14), tables.options, hero).HasValue());
    EXPECT_EQ(OptionsIn(state), (Listed{{7, 9}}));

    const auto outside = Client::Player::ApplyItemSetOptions(state, OnePiece(5, 31), tables.options, hero);
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("31 piezas"), std::string::npos);
    EXPECT_NE(outside.Error().find("Q-C187"), std::string::npos);
}

// 0x40d35e..0x40d457: complete with pieces − 1 ≥ +0x3c − 2 (+0x3c is the pieces + 1), it adds the complete set's
// options and sets the published hero's +0x358 to 1. With one less, it does not.
TEST(OwnItemSetsTest, AFullSetAddsItsOptionsAndMarksThePublishedHero)
{
    SetOptions options;
    options.Named(3, "Tres", 4).Pair(3, 0, 2, 15).Pair(3, 1, 1, 15).Full(3, 0, 0x13, 20).Full(3, 1, 0x0c, 30);
    const ItemSetTables tables = TablesOf(SetTypes{}, options);
    ActorState hero;
    hero.byte358 = 7;
    ItemSetState state;

    ASSERT_TRUE(Client::Player::ApplyItemSetOptions(state, OnePiece(3, 2), tables.options, hero).HasValue());
    EXPECT_EQ(OptionsIn(state), (Listed{{2, 15}}));
    EXPECT_EQ(hero.byte358, 0);

    ASSERT_TRUE(Client::Player::ApplyItemSetOptions(state, OnePiece(3, 3), tables.options, hero).HasValue());
    EXPECT_EQ(OptionsIn(state), (Listed{{2, 15}, {1, 15}, {0x13, 20}, {0x0c, 30}}));
    EXPECT_EQ(hero.byte358, 1);
    EXPECT_EQ(NameIn(state.names[0]), "Tres");
    EXPECT_EQ(state.list.setIds[0], 3);
    EXPECT_EQ(state.byte9307, 0);

    // +0x3c at 0: −2, complete from two pieces.
    options.Byte(3, 0x3c, 0);
    const ItemSetTables anyCount = TablesOf(SetTypes{}, options);
    ASSERT_TRUE(Client::Player::ApplyItemSetOptions(state, OnePiece(3, 2), anyCount.options, hero).HasValue());
    EXPECT_EQ(OptionsIn(state), (Listed{{2, 15}, {0x13, 20}, {0x0c, 30}}));
    EXPECT_EQ(hero.byte358, 1);
}

// 0x40d0d0..0x40d14d and 0x40d29c..0x40d2bd, in every class (+0x1d4 & 7) with and without evolution (>> 3): the class
// mark is rec[0x47 + class]. An option from 0x18 on enters with the mark other than 0 and the evolution ≥ mark − 1.
// The list's flag is 1 in classes 0 to 2 with the mark at 1, or at 2 with evolution, and in class 3 only with 1; class
// 4 never. Classes 5 to 7 read the mark in the next record.
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
        ASSERT_TRUE(Client::Player::ApplyItemSetOptions(state, OnePiece(9, 2), tables.options, hero).HasValue());
        EXPECT_EQ(OptionsIn(state), (expected.allowed ? Listed{{0x30, 40}, {4, 41}} : Listed{{4, 41}}));
        EXPECT_EQ(state.list.entries[0][1], expected.flag);
    }

    // Classes 5 to 7 fall in bytes 0 to 2 of the next record, which here are 1, 32 and 0: with the evolution at 31, 5
    // and 6 (31 ≥ 32 − 1) enter and 7 does not.
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
        ASSERT_TRUE(Client::Player::ApplyItemSetOptions(state, OnePiece(9, 2), tables.options, hero).HasValue());
        EXPECT_EQ(OptionsIn(state), (heroClass == 7 ? Listed{} : Listed{{0x30, 40}}));
        if (heroClass != 7)
        {
            EXPECT_EQ(state.list.entries[0][1], 0);
        }
    }
}

// After the last set, class 5's mark falls outside the table (Q-C187). An option from 0x18 on reads it; the ones
// before do not.
TEST(OwnItemSetsTest, TheMarkPastTheLastSetIsAnError)
{
    SetOptions options;
    options.Named(63, "Ultimo", 0xff).Pair(63, 0, 1, 5).Pair(63, 1, 0x18, 6);
    const ItemSetTables tables = TablesOf(SetTypes{}, options);
    ActorState hero;
    hero.characterClass = 5;
    ItemSetState state;
    ASSERT_TRUE(Client::Player::ApplyItemSetOptions(state, OnePiece(63, 2), tables.options, hero).HasValue());
    const auto outside = Client::Player::ApplyItemSetOptions(state, OnePiece(63, 3), tables.options, hero);
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("Q-C187"), std::string::npos);
    EXPECT_EQ(state.list.count9348, 0);
}

// 0x40d16d..0x40d233: the name goes to its piece kind. Another set of the same kind goes to the first empty name or to
// the second, and leaves +0x9307 at its pieces − 1; the complete set's options add 1 to +0x9307 if it is not 0. Kind
// 0's options go first and kind 1's after them.
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

    ASSERT_TRUE(Client::Player::ApplyItemSetOptions(state, pieces, tables.options, hero).HasValue());

    // "Cuatro" in kind 1's; "Uno" in kind 0's; "Dos", of kind 0 with another name there, in the second.
    EXPECT_EQ(NameIn(state.names[0]), "Uno");
    EXPECT_EQ(NameIn(state.names[1]), "Dos");
    // strcpy copies the name with its 0 over "Cuatro" and leaves what follows: "Dos\0ro\0".
    EXPECT_EQ(std::string(state.names[1].data() + 4), "ro");
    EXPECT_EQ(state.list.setIds[0], 2);
    EXPECT_EQ(state.list.setIds[1], 4);
    // +0x9307: 2 when "Dos" enters and + 1 for each of its two complete-set options.
    EXPECT_EQ(state.byte9307, 4);
    // Of kind 0, Uno with one pair and one complete-set option, and Dos with two pairs and two.
    EXPECT_EQ(state.list.count9348, 6);
    EXPECT_EQ(state.list.count9349, 1);
    EXPECT_EQ(OptionsIn(state), (Listed{{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {0, 6}, {4, 7}}));
    EXPECT_EQ(hero.byte358, 1);

    // The same set again keeps its name and does not touch +0x9307.
    const ItemSetPieces again = {ItemSetPiece{.setId = 1, .count = 2, .kind = 0}, ItemSetPiece{1, 2, 0}, ItemSetPiece{},
                                 ItemSetPiece{}};
    ASSERT_TRUE(Client::Player::ApplyItemSetOptions(state, again, tables.options, hero).HasValue());
    EXPECT_EQ(NameIn(state.names[0]), "Uno");
    EXPECT_EQ(NameIn(state.names[1]), "");
    EXPECT_EQ(state.byte9307, 0);
}

// What the original reads or writes outside its tables and lists is an error (Q-C187): a set of 64 or more, piece kind
// 2 (& 3 at 3), a name with no 0 in its 0x20 bytes, more than 16 options of one kind or more than 16 between both. The
// counts stay at 0, as 0x40d010 leaves them on entry.
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
    // With four pieces, 6 pair options and 5 complete-set options per set: 11 + 11.
    const ItemSetPieces twoKinds = {ItemSetPiece{3, 4, 0}, ItemSetPiece{4, 4, 1}, ItemSetPiece{}, ItemSetPiece{}};
    for (const Case& outside :
         {Case{OnePiece(64, 2), "conjunto 64"}, Case{OnePiece(1, 2, 2), "clase de pieza 2"},
          Case{OnePiece(2, 2), "no termina"}, Case{OnePiece(3, 7), "más de 16"}, Case{twoKinds, "no entran"}})
    {
        SCOPED_TRACE(outside.shown);
        ItemSetState state;
        state.list.count9348 = 9;
        state.list.count9349 = 9;
        const auto applied = Client::Player::ApplyItemSetOptions(state, outside.pieces, tables.options, hero);
        ASSERT_FALSE(applied.HasValue());
        EXPECT_NE(applied.Error().find("Q-C187"), std::string::npos);
        EXPECT_NE(applied.Error().find(outside.shown), std::string::npos);
        EXPECT_EQ(state.list.count9348, 0);
        EXPECT_EQ(state.list.count9349, 0);
    }
}

// 0x550d29..0x550d3f: the marks of +0x937c at 0, and nothing else of the list. What 0x40da20 (0x40d690's code) already
// added can be added again after the clearing.
TEST(OwnItemSetsTest, ClearingTheMarksLetsTheOptionsAddAgain)
{
    ItemSetState state;
    state.list.count9348 = 2;
    state.list.entries[0] = {1, 1, 0};
    state.list.entries[1] = {1, 0, 0};
    state.list.values[0] = 10;
    state.list.values[1] = 20;
    std::uint16_t word = 0;
    ASSERT_TRUE(Client::Player::AddItemSetOptionValues(state.list, word, 0x8d).HasValue());
    ASSERT_TRUE(Client::Player::AddItemSetOptionValues(state.list, word, 0x8d).HasValue());
    EXPECT_EQ(word, 30);
    EXPECT_EQ(state.list.used, (std::array<std::uint8_t, 16>{1, 1}));

    Client::Player::ClearItemSetOptionMarks(state.list);

    EXPECT_EQ(state.list.used, (std::array<std::uint8_t, 16>{}));
    EXPECT_EQ(state.list.count9348, 2);
    EXPECT_EQ(state.list.entries[0][1], 1);
    EXPECT_EQ(state.list.values[1], 20);
    ASSERT_TRUE(Client::Player::AddItemSetOptionValues(state.list, word, 0x8d).HasValue());
    EXPECT_EQ(word, 60);
}

// An own record with what 0x40e1b0 reads: every place empty.
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

// What 0x40e1b0 asks for, with the fixture's access count and rand().
class OwnItemSetsFixture : public ::testing::Test
{
protected:
    Client::Player::OwnStatsContext Context(const ItemSetTables* tables)
    {
        return {.itemSetTables = tables,
                .itemSets = &m_state,
                .items = &m_items,
                .publishedHero = m_hero,
                .ownRecordAccesses = m_accesses,
                .random = m_random};
    }

    ItemSetState m_state;
    Client::Tables::ItemTable m_items = Client::Tests::ItemsMarking({});
    ActorState m_hero;
    std::uint8_t m_accesses = 1;
    CrtRandom m_random{3};
};

// 0x40de10: the twelve places (the wings and the pet too) with durability, type 1 to the strength, 2 to the agility, 3
// to the energy and 4 to the vitality, 5 · value; then options 0 to 3 of the list, which stay marked. It opens and
// closes the record at each place: with the count at 0, 12 closings.
TEST_F(OwnItemSetsFixture, StatCopiesTakeTheItemBonusesAndTheSetOptions)
{
    // Item.bmd's byte +0x44: type 1 at 1, 2 at 2, 3 at 3 and 4 at 4.
    std::vector<std::byte> plain(Client::Tables::ItemTableSize);
    for (std::size_t type = 1; type <= 4; ++type)
    {
        plain[type * Client::Tables::ItemRecordSize + 0x44] = static_cast<std::byte>(type);
    }
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Tables::ItemRecordSize);
    m_items = Client::Tables::ItemTable(std::span<const std::byte>(encrypted));
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
    // A place without durability does not read Item.bmd, even with its type outside.
    own.equipment[5].id = EmptyType;
    m_state.list.count9348 = 4;
    for (std::size_t index = 0; index < 4; ++index)
    {
        m_state.list.entries[index][0] = static_cast<std::uint8_t>(index);
        m_state.list.values[index] = static_cast<std::int32_t>(100 * (index + 1));
    }
    m_accesses = 0;
    OwnStatCopies copies{.strength = 1, .agility = 2, .vitality = 3, .energy = 4};

    ASSERT_TRUE(Client::Player::AddOwnBonusesToStatCopies(copies, own, Context(nullptr)).HasValue());

    EXPECT_EQ(copies.strength, 1 + 15 + 5 + 100);
    EXPECT_EQ(copies.agility, 2 + 10 + 200);
    EXPECT_EQ(copies.energy, 4 + 5 + 300);
    EXPECT_EQ(copies.vitality, 3 + 15 + 400);
    EXPECT_EQ(m_state.list.used, (std::array<std::uint8_t, 16>{1, 1, 1, 1}));
    EXPECT_EQ(m_accesses, 0);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(3, 12 * OwnRecordCloseRandoms).State());
}

// Q-C184 from 0x40de10: an emptied place with durability stops, with the access closed.
TEST_F(OwnItemSetsFixture, AnEmptySlotWithDurabilityStopsTheStatCopies)
{
    OwnCharacterRecord own = Unequipped();
    own.equipment[4].durability = 1;
    own.equipment[4].flags1c = 4; // Value 1: the unknown byte can change the copies.
    OwnStatCopies copies;
    const auto added = Client::Player::AddOwnBonusesToStatCopies(copies, own, Context(nullptr));
    ASSERT_FALSE(added.HasValue());
    EXPECT_NE(added.Error().find("0x40de10, lugar 4"), std::string::npos);
    EXPECT_NE(added.Error().find("Q-C184"), std::string::npos);
    EXPECT_EQ(m_accesses, 1);
}

// 0x40e033..0x40e075: an unknown selector times value 0 changes no copy. Each place keeps its opening and closing,
// though it does not need to know the selector.
TEST_F(OwnItemSetsFixture, AnEmptySlotWithZeroBonusPreservesCopiesAndBothLogicalAccessCases)
{
    for (const std::uint8_t depth : {std::uint8_t{0}, std::uint8_t{1}})
    {
        OwnCharacterRecord own = Unequipped();
        own.equipment[0].durability = 25;
        own.equipment[0].flags1c = 0xf3;
        m_accesses = depth;
        m_random = CrtRandom(3);
        OwnStatCopies copies{.strength = 11, .agility = 22, .vitality = 44, .energy = 33};

        const auto added = Client::Player::AddOwnBonusesToStatCopies(copies, own, Context(nullptr));

        ASSERT_TRUE(added.HasValue()) << added.Error();
        EXPECT_EQ(copies.strength, 11);
        EXPECT_EQ(copies.agility, 22);
        EXPECT_EQ(copies.energy, 33);
        EXPECT_EQ(copies.vitality, 44);
        EXPECT_EQ(m_accesses, depth);
        EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(3, depth == 0 ? 12 * OwnRecordCloseRandoms : 0).State());
        EXPECT_EQ(own.equipment[0].id, EmptyType);
        EXPECT_EQ(own.equipment[0].durability, 25);
        EXPECT_EQ(own.equipment[0].flags1c, 0xf3);
    }
}

// Without the tables or without [0x6eed3c]'s state, 0x40e1b0 stops before touching anything: not the list, the hero
// or an access.
TEST_F(OwnItemSetsFixture, WithoutTheTablesNothingIsTouched)
{
    m_state.list.entries[0][0] = 0x42;
    m_hero.byte358 = 3;
    m_accesses = 0;
    const auto recalculated = Client::Player::RecalculateOwnItemSets(Unequipped(), Context(nullptr));
    ASSERT_FALSE(recalculated.HasValue());
    EXPECT_NE(recalculated.Error().find("0x40c600"), std::string::npos);
    EXPECT_EQ(m_state.list.entries[0][0], 0x42);
    EXPECT_EQ(m_hero.byte358, 3);
    EXPECT_EQ(m_random.State(), CrtRandom(3).State());

    // With the tables and nowhere to write, the same.
    const ItemSetTables tables = TablesOf(SetTypes{}, SetOptions{});
    Client::Player::OwnStatsContext withoutState = Context(&tables);
    withoutState.itemSets = nullptr;
    const auto stateless = Client::Player::RecalculateOwnItemSets(Unequipped(), withoutState);
    ASSERT_FALSE(stateless.HasValue());
    EXPECT_NE(stateless.Error().find("WorldState::ItemSets"), std::string::npos);
    EXPECT_EQ(m_hero.byte358, 3);
    EXPECT_EQ(m_random.State(), CrtRandom(3).State());
}

// The whole 0x40e1b0, with three sets: the wings and the pet do not count; the same item in both hands or in both
// rings counts once, and with another set, twice. It opens and closes the record 10 + 1 + 12 + 10 times: with the
// count at 0, 33 closings of 3,848 rand() calls.
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
    own.equipment[1] = Piece(0x10, 1); // the same as 0: does not count
    own.equipment[2] = Piece(0x11, 1);
    own.equipment[7] = Piece(0x11, 1); // the wings do not count
    own.equipment[8] = Piece(0x30, 1); // nor does the pet
    own.equipment[9] = Piece(0x30, 1);
    own.equipment[10] = Piece(0x20, 1);
    own.equipment[11] = Piece(0x20, 2); // another set of the same type: counts
    m_accesses = 0;

    ASSERT_TRUE(Client::Player::RecalculateOwnItemSets(own, Context(&tables)).HasValue());

    // Uno with two pieces (0x10 and 0x11); Dos with two (kind 1's last); Tres with one, no options.
    EXPECT_EQ(OptionsIn(m_state), (Listed{{0, 1}, {3, 4}}));
    EXPECT_EQ(m_state.list.count9348, 1);
    EXPECT_EQ(m_state.list.count9349, 1);
    EXPECT_EQ(NameIn(m_state.names[0]), "Uno");
    EXPECT_EQ(NameIn(m_state.names[1]), "Dos");
    EXPECT_EQ(m_hero.byte358, 0);
    EXPECT_EQ(m_accesses, 0);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(3, 33 * OwnRecordCloseRandoms).State());
}

// The second pass skips the items whose requirements (+0x1e the strength, +0x20 the agility, +0x22 the energy;
// unsigned) exceed the copies, which already carry the first pass's: a piece that only fits thanks to its own set's
// option counts. After 0x40e4bf's 0xff, the list is the second pass's.
TEST_F(OwnItemSetsFixture, TheSecondPassChecksRequirementsAgainstTheBoostedCopies)
{
    SetTypes types;
    types.Set(0x10, 0, 1).Set(0x11, 0, 1).Set(0x12, 0, 1);
    SetOptions options;
    // With two pieces, +10 to the agility (option 1, 0x8d); with three, +10 to the energy (option 2).
    options.Named(1, "Uno", 0xff).Pair(1, 0, 1, 10).Pair(1, 1, 2, 10);
    const ItemSetTables tables = TablesOf(types, options);
    OwnCharacterRecord own = Unequipped();
    own.strength = 50;
    own.agility = 20;
    own.energy = 40;
    own.equipment[2] = Piece(0x10, 1);
    own.equipment[3] = Piece(0x11, 1);
    own.equipment[3].word20 = 30; // fits with option 1's 10
    own.equipment[4] = Piece(0x12, 1);
    own.equipment[4].word22 = 51; // does not fit: the energy reaches 50 in the first pass

    ASSERT_TRUE(Client::Player::RecalculateOwnItemSets(own, Context(&tables)).HasValue());

    EXPECT_EQ(OptionsIn(m_state), (Listed{{1, 10}}));
    // 0x40de10's marks stay from the first pass; the second's 0xff, where it did not write.
    EXPECT_EQ(m_state.list.used[0], 1);
    EXPECT_EQ(m_state.list.used[1], 1);
    EXPECT_EQ(m_state.list.entries[0][2], 0xff);
    EXPECT_EQ(m_state.list.entries[1][0], 0xff);
    EXPECT_EQ(m_state.list.entries[5][0], 0xff);
    EXPECT_EQ(m_state.list.entries[5][1], 0);
}

// -0x7c and -0x60 carry over from one pass to the other: if place 0 does not meet its requirements in the second, 1 is
// compared with the first pass's 10, and the same item in 1 and in 10 counts once instead of twice.
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
    // The strength is at 0 and the copy reaches 1 with Uno's option 0: it does not fit in the second pass.
    own.equipment[0].word1e = 2;
    own.equipment[1] = Piece(0x10, 1);
    own.equipment[9] = Piece(0x20, 1);
    own.equipment[10] = Piece(0x10, 1);
    own.equipment[11] = Piece(0x10, 1);

    ASSERT_TRUE(Client::Player::RecalculateOwnItemSets(own, Context(&tables)).HasValue());

    // First pass: Uno with 2 (1 and 10; 11 repeats 10) and Dos with 2 (0 and 9). Second: 1 repeats the first pass's
    // 10, so Uno is left with 1 (10), and Dos with 1 (9).
    EXPECT_EQ(OptionsIn(m_state), Listed{});
    EXPECT_EQ(m_state.list.setIds[0], 0);
}
} // namespace
