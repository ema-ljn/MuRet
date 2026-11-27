#include "Player/OwnDerivedStats.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "Player/ItemSetOptionList.h"
#include "Player/ItemSetState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Player/OwnStatHelpers.h"
#include "Kernel/ItemState.h"
#include "support/ItemSetFiles.h"
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

// Client::Player::RecalculateOwnStats and the steps of 0x550d10 that MuRet has (06, "Los stats del registro propio";
// 07, Q-C184, Q-C185 and Q-C187). The values come from the ASM checked against the PE c32bbd5f, and the rand() calls
// from the CRT's model; none from a run of the original.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::ItemState;
using Client::Kernel::OwnCharacterRecord;
using Client::Models::OwnRecordCloseRandoms;
using Client::Player::ItemSetOptionList;
using Client::Player::ItemSetState;
using Client::Tables::ItemSetTables;
using Client::Tests::AfterDraws;
using Client::Tests::FirstSeedWhere;

constexpr std::size_t KindOffset = 0x44;
constexpr std::size_t WingsSlot = 7;
constexpr std::uint16_t CapeOfLord = 0x1a1e;
constexpr std::uint16_t EmptyType = 0xffff;

// An Item.bmd type and its byte +0x44. An aggregate and not std::pair: pair's constructor from int narrows inside
// <utility> (C4242 on MSVC).
struct TypeKind
{
    std::uint16_t type;
    std::uint8_t kind;
};

// A synthetic Item.bmd: byte +0x44 of each type in `kinds` and everything else at 0.
Client::Tables::ItemTable ItemsWithKinds(std::initializer_list<TypeKind> kinds)
{
    std::vector<std::byte> plain(Client::Tables::ItemTableSize);
    for (const auto& [type, kind] : kinds)
    {
        plain[type * Client::Tables::ItemRecordSize + KindOffset] = std::byte{kind};
    }
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Tables::ItemRecordSize);
    return Client::Tables::ItemTable(std::span<const std::byte>(encrypted));
}

// An item with durability and value `value` in bits 2 and 3 of byte +0x1c.
ItemState Worn(std::uint16_t type, std::uint8_t value, std::uint8_t durability = 1)
{
    ItemState item;
    item.id = type;
    item.durability = durability;
    item.flags1c = static_cast<std::uint8_t>(value << 2);
    return item;
}

// What 0x550d10 reads and writes outside the record, with the access count at 0: each closing draws 3,848 rand() calls.
class OwnStatsFixture : public ::testing::Test
{
protected:
    Client::Player::OwnStatsContext Context(const ItemSetTables* tables, const Client::Tables::ItemTable* items)
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

// 0x550d24: 0x40e1b0 comes first. Without the sets' tables (0x40c600) it stops before touching anything: not the
// record, [0x6eed3c], the hero or a rand().
TEST_F(OwnStatsFixture, WithoutTheSetTablesTheRecalculationStopsBeforeTouchingAnything)
{
    OwnCharacterRecord own;
    own.word2a = 0x1234;
    own.word36 = 0x2345;
    own.wordef8 = 0x3456;
    own.bytef02 = 7;
    m_itemSets.list.used[0] = 1;
    m_hero.byte358 = 4;

    const auto recalculated = Client::Player::RecalculateOwnStats(own, Context(nullptr, nullptr));

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

// 0x550d10 in order up to 0x550990: 0x40e1b0 (two pieces of set 1, with options 0 and 4), the marks of +0x937c at 0,
// 0x54ef00, the twelve places (type 1 adds to +0x2c), 0x40da20 with 0x8c to 0x90, Cape of Lord and the first five
// helpers. 0x40de10 already marked option 0 on the copies: if the marks did not go back to 0, +0x2a would not add it.
// Without knowing what 0x505820 left (scan505820 at nullopt) it stops at 0x550990, without the tail: 33 closings of
// 0x40e1b0 and the cape's, and no other rand().
TEST_F(OwnStatsFixture, TheRecalculationRunsInOrderUntil0x550990)
{
    std::vector<std::byte> types(Client::Tables::ItemSetTypeTableSize, std::byte{0xff});
    types[1 * Client::Tables::ItemSetTypeRecordSize] = std::byte{1};
    std::vector<std::byte> options(Client::Tables::ItemSetOptionTableSize, std::byte{0xff});
    const std::span<std::byte> set1 =
        std::span(options).subspan(Client::Tables::ItemSetOptionRecordSize, Client::Tables::ItemSetOptionRecordSize);
    set1[0] = std::byte{'U'};
    set1[1] = std::byte{0};
    set1[0x20] = std::byte{0}; // option 0 (0x8c, +0x2a) with 7
    set1[0x21] = std::byte{4}; // option 4 (0x90, +0x36) with 9
    set1[0x2c] = std::byte{7};
    set1[0x2d] = std::byte{9};
    const ItemSetTables tables = Client::Tests::ItemSetTablesFrom(std::move(types), std::move(options));
    const auto items = ItemsWithKinds({{1, 2}});
    OwnCharacterRecord own;
    own.equipment.fill(Worn(EmptyType, 0, 0));
    own.equipment[2] = Worn(1, 1);
    own.equipment[2].flags1c = 0x05; // value 1 in bits 2 and 3, and byte 0's set
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

    const auto recalculated = Client::Player::RecalculateOwnStats(own, Context(&tables, &items));

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

// With 0x505820's result (here, [0x743a17c] at 0), 0x550d10 reaches the end: the eight helpers in their order and the
// tail. With no equipment or sets, 0x40e1b0 closes the record 33 times; the tail draws its three rand() calls: the
// IDIV's with +0xee2 = +0xee4 = 0 (divides by 1) and the two % 100. +0xef8 and +0xefa are subtractions of WORDs at 0,
// and +0xefc is 0 − +0x62.
TEST_F(OwnStatsFixture, WithTheScanOf0x505820TheRecalculationRunsTheHelpersAndTheTail)
{
    const ItemSetTables tables = Client::Tests::ItemSetTablesFrom(
        std::vector<std::byte>(Client::Tables::ItemSetTypeTableSize, std::byte{0xff}),
        std::vector<std::byte>(Client::Tables::ItemSetOptionTableSize, std::byte{0xff}));
    const auto items = ItemsWithKinds({});
    OwnCharacterRecord own;
    own.equipment.fill(Worn(EmptyType, 0, 0));
    own.wordef8 = 0x3456;
    own.wordefa = 0x4567;
    Client::Player::OwnStatsContext context = Context(&tables, &items);
    context.scan505820 = Client::Actors::Scan505820{};

    const auto recalculated = Client::Player::RecalculateOwnStats(own, context);

    ASSERT_TRUE(recalculated.HasValue()) << recalculated.Error();
    EXPECT_EQ(own.wordef8, 0);
    EXPECT_EQ(own.wordefa, 0);
    EXPECT_EQ(own.wordefc, static_cast<std::uint16_t>(0u - own.word62));
    EXPECT_EQ(m_accesses, 0);
    EXPECT_EQ(m_random.State(), AfterDraws(Seed, 33 * OwnRecordCloseRandoms + 3).State());
}

// 0x550dc2..0x550e12: +0x2a with 0x8c, +0x2c with 0x8d, +0x30 with 0x8e, +0x2e with 0x8f and +0x36 with 0x90; +0x32
// and +0x34 have no set option. With more than 16 options, an error at the first call.
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

    ASSERT_TRUE(Client::Player::AddOwnItemSetOptionValues(own, list).HasValue());

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
    const auto outside = Client::Player::AddOwnItemSetOptionValues(own, list);
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("0x40da20 con 0x8c"), std::string::npos);
    EXPECT_EQ(own.word2a, 0x101);
}

// 0x54ef00: the seven WORDs from +0x2a to +0x36 at 0; +0x28 and +0x38, the neighbors, stay.
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

    Client::Player::ClearOwnItemStatWords(own);

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

// 0x40dc50: the value is (D+0x1c >> 2) & 3, without the other bits, and the type is byte +0x44 of the item type's
// Item.bmd record.
TEST(OwnDerivedStatsTest, TheItemBonusIsBits2And3OfByte1cAndTheKindAt44)
{
    const auto items = ItemsWithKinds({{0x10, 3}, {0x1fff, 0xa7}});
    ItemState item;
    item.id = 0x10;
    item.flags1c = 0xf3; // 1111 0011: bits 2 and 3 at 0
    auto bonus = Client::Player::ItemStatBonusOf(item, items);
    ASSERT_TRUE(bonus.HasValue());
    EXPECT_EQ(bonus.Value().kind, 3);
    EXPECT_EQ(bonus.Value().value, 0);

    item.flags1c = 0x0c;
    bonus = Client::Player::ItemStatBonusOf(item, items);
    ASSERT_TRUE(bonus.HasValue());
    EXPECT_EQ(bonus.Value().value, 3);

    // The table's last type, 8191, is still inside.
    item.id = 0x1fff;
    item.flags1c = 0x18;
    bonus = Client::Player::ItemStatBonusOf(item, items);
    ASSERT_TRUE(bonus.HasValue());
    EXPECT_EQ(bonus.Value().kind, 0xa7);
    EXPECT_EQ(bonus.Value().value, 2);
}

// Q-C184: movsx and the 8,192-entry table: with −1 (an emptied place), a negative type or one of 8,192 or more, the
// original reads memory that is not the table's. MuRet does not invent that byte.
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
        const auto bonus = Client::Player::ItemStatBonusOf(Worn(outside.type, 1), items);
        ASSERT_FALSE(bonus.HasValue());
        EXPECT_NE(bonus.Error().find("Q-C184"), std::string::npos);
        EXPECT_NE(bonus.Error().find(outside.shown), std::string::npos);
    }
}

// 0x550d72..0x550da9: with value 0, any of the 256 selectors adds 0 or does not add. The byte at base(Item.bmd)-16 is
// unknown; the consumer does not need it on this branch either.
TEST(OwnDerivedStatsTest, AnEmptySlotWithZeroBonusPreservesItsResidualDataAndAllStatWords)
{
    const auto items = ItemsWithKinds({});
    constexpr std::array<std::uint8_t, 5> FlagsWithoutBonus{0, 1, 3, 0x10, 0xf3};
    for (const std::uint8_t flags : FlagsWithoutBonus)
    {
        OwnCharacterRecord own;
        own.word2a = 11;
        own.word2c = 22;
        own.word30 = 33;
        own.word2e = 44;
        own.equipment[0] = Worn(EmptyType, 0, 25);
        own.equipment[0].flags1c = flags;

        const auto added = Client::Player::AddOwnItemStatBonuses(own, &items);

        ASSERT_TRUE(added.HasValue()) << added.Error();
        EXPECT_EQ(own.word2a, 11);
        EXPECT_EQ(own.word2c, 22);
        EXPECT_EQ(own.word30, 33);
        EXPECT_EQ(own.word2e, 44);
        EXPECT_EQ(own.equipment[0].id, EmptyType);
        EXPECT_EQ(own.equipment[0].durability, 25);
        EXPECT_EQ(own.equipment[0].flags1c, flags);
    }
}

// 0x550d49..0x550db1: the twelve places, also 9 to 11, which 0x512a30 does not read; without durability it does not
// call 0x40dc50. Types 1 to 4 add 5 · value to +0x2a, +0x2c, +0x30 and +0x2e (0x5513b4's jump table), in 16 bits; the
// others, nothing.
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

    ASSERT_TRUE(Client::Player::AddOwnItemStatBonuses(own, &items).HasValue());

    EXPECT_EQ(own.word2a, 20); // 0xfff6 + 15 wraps to 5, and place 11 adds another 15
    EXPECT_EQ(own.word2c, 10);
    EXPECT_EQ(own.word30, 5);
    EXPECT_EQ(own.word2e, 25);
    EXPECT_EQ(own.word32, 0x32);
    EXPECT_EQ(own.word34, 0);
    EXPECT_EQ(own.word36, 0);
}

// A place that stops keeps what the earlier ones added, and goes no further. Without Item.bmd, it stops at the first
// place that reads it; if none has durability, none reads it.
TEST(OwnDerivedStatsTest, ABonusErrorKeepsWhatTheEarlierSlotsAdded)
{
    const auto items = ItemsWithKinds({{1, 1}, {2, 2}});
    OwnCharacterRecord own;
    own.equipment.fill(Worn(EmptyType, 0, 0));
    own.equipment[0] = Worn(1, 1);
    own.equipment[1] = Worn(EmptyType, 1);
    own.equipment[2] = Worn(2, 1);

    const auto added = Client::Player::AddOwnItemStatBonuses(own, &items);

    ASSERT_FALSE(added.HasValue());
    EXPECT_NE(added.Error().find("lugar 1"), std::string::npos);
    EXPECT_NE(added.Error().find("Q-C184"), std::string::npos);
    EXPECT_EQ(own.word2a, 5);
    EXPECT_EQ(own.word2c, 0);

    OwnCharacterRecord bare;
    bare.equipment.fill(Worn(1, 3, 0));
    EXPECT_TRUE(Client::Player::AddOwnItemStatBonuses(bare, nullptr).HasValue());
    bare.equipment[4].durability = 1;
    const auto unloaded = Client::Player::AddOwnItemStatBonuses(bare, nullptr);
    ASSERT_FALSE(unloaded.HasValue());
    EXPECT_NE(unloaded.Error().find("Item.bmd"), std::string::npos);
    EXPECT_NE(unloaded.Error().find("lugar 4"), std::string::npos);
}

// 0x550e17..0x551284: with 0x1a1e in place 7, it opens the record, 0x54e540 adds the values of the 0x90 options to
// +0x36 (with durability, 16 bits) and closes it: with the count at 0, 3,848 rand() calls; with the record opened by
// the caller, as in 0x512a30, none.
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

    ASSERT_TRUE(Client::Player::AddCapeOfLordOptions(own, accesses, random).HasValue());
    EXPECT_EQ(own.word36, 24); // 0xfff0 + 10 + 30
    EXPECT_EQ(accesses, 0);
    EXPECT_EQ(random.State(), AfterDraws(Seed, OwnRecordCloseRandoms).State());

    accesses = 1;
    random = CrtRandom(Seed);
    ASSERT_TRUE(Client::Player::AddCapeOfLordOptions(own, accesses, random).HasValue());
    EXPECT_EQ(own.word36, 64);
    EXPECT_EQ(accesses, 1);
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());

    // Without durability 0x54e540 adds nothing, but the record is opened and closed anyway.
    wings.durability = 0;
    random = CrtRandom(Seed);
    accesses = 0;
    ASSERT_TRUE(Client::Player::AddCapeOfLordOptions(own, accesses, random).HasValue());
    EXPECT_EQ(own.word36, 64);
    EXPECT_EQ(random.State(), AfterDraws(Seed, OwnRecordCloseRandoms).State());
}

// With another type in the wings, even with option 0x90, it neither opens the record nor adds.
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
        ASSERT_TRUE(Client::Player::AddCapeOfLordOptions(own, accesses, random).HasValue());
        EXPECT_EQ(own.word36, 0);
        EXPECT_EQ(accesses, 0);
        EXPECT_EQ(random.State(), CrtRandom(3).State());
    }
}

// More than nine options: 0x54e540 would keep reading into place 8, outside the projection. An error, after opening,
// and with the access closed.
TEST(OwnDerivedStatsTest, ACapeWithMoreThanNineOptionsIsAnExplicitErrorWithTheRecordClosed)
{
    OwnCharacterRecord own;
    ItemState& wings = own.equipment[WingsSlot];
    wings.id = CapeOfLord;
    wings.durability = 1;
    wings.optionCount = 10;
    std::uint8_t accesses = 0;
    CrtRandom random(5);

    const auto added = Client::Player::AddCapeOfLordOptions(own, accesses, random);

    ASSERT_FALSE(added.HasValue());
    EXPECT_NE(added.Error().find("10 opciones"), std::string::npos);
    EXPECT_EQ(accesses, 0);
    EXPECT_EQ(random.State(), AfterDraws(5, OwnRecordCloseRandoms).State());
}

// The tail with a one-value range: the remainder is 0 whatever the rand(). The subtractions wrap, +0xefe's cap leaves
// 100 (and +0xf02 at 1 with any draw) and +0xf00 at 0 leaves +0xf03 at 0. It draws three.
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

    ASSERT_TRUE(Client::Player::UpdateOwnDerivedStatsTail(own, random).HasValue());

    EXPECT_EQ(own.wordef8, 0xfffe);
    EXPECT_EQ(own.wordefa, 9);
    EXPECT_EQ(own.wordefc, 0xff2c); // 300 − 0x200
    EXPECT_EQ(own.wordefe, 100);    // 3 − 5 = 0xfffe, unsigned, to the cap
    EXPECT_EQ(own.wordf00, 0);
    EXPECT_EQ(own.bytef02, 1);
    EXPECT_EQ(own.bytef03, 0);
    EXPECT_EQ(random.State(), AfterDraws(Seed, 3).State());
}

// The cap is "above 100" (jbe): 100 stays and 101 drops. The draws compare rand() % 100 with < (jge reversed): with
// 50, a 49 gives 1 and a 50 gives 0.
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
    ASSERT_TRUE(Client::Player::UpdateOwnDerivedStatsTail(own, random).HasValue());
    EXPECT_EQ(own.wordefe, 50);
    EXPECT_EQ(own.wordf00, 50);
    EXPECT_EQ(own.bytef02, 1);
    EXPECT_EQ(own.bytef03, 0);

    own.word4e = 100;
    own.wordeec = 0;
    own.word60 = 101;
    own.wordeea = 0;
    ASSERT_TRUE(Client::Player::UpdateOwnDerivedStatsTail(own, random).HasValue());
    EXPECT_EQ(own.wordefe, 100);
    EXPECT_EQ(own.wordf00, 100);
}

// The range uses the two unsigned WORDs and a signed IDIV: from 10 to 19, rand() % 10 + 10; with +0xee2 above
// +0xee4 + 1 the divisor is negative and the remainder follows the rand() (0 to 32,767).
TEST(OwnDerivedStatsTest, TheTailRangeIsASignedDivision)
{
    constexpr std::uint32_t Seed = 23;
    const int first = CrtRandom(Seed).Next();
    OwnCharacterRecord own;
    own.wordee2 = 10;
    own.wordee4 = 19;
    own.word62 = 4;
    CrtRandom random(Seed);
    ASSERT_TRUE(Client::Player::UpdateOwnDerivedStatsTail(own, random).HasValue());
    EXPECT_EQ(own.wordefc, static_cast<std::uint16_t>(first % 10 + 10 - 4));

    // 0 − 65,535 + 1 = −65,534: the remainder is the whole rand(), and + 65,535 in 16 bits is − 1.
    own.wordee2 = 0xffff;
    own.wordee4 = 0;
    own.word62 = 0;
    random = CrtRandom(Seed);
    ASSERT_TRUE(Client::Player::UpdateOwnDerivedStatsTail(own, random).HasValue());
    EXPECT_EQ(own.wordefc, static_cast<std::uint16_t>(first - 1));

    // 0xffff − 0 + 1 = 65,536: it does not limit the rand() either.
    own.wordee2 = 0;
    own.wordee4 = 0xffff;
    random = CrtRandom(Seed);
    ASSERT_TRUE(Client::Player::UpdateOwnDerivedStatsTail(own, random).HasValue());
    EXPECT_EQ(own.wordefc, static_cast<std::uint16_t>(first));
}

// Q-C185: with +0xee2 = +0xee4 + 1 the IDIV of 0x551306 divides by 0. +0xef8, +0xefa and the first rand() are done;
// the rest stays.
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

    const auto tail = Client::Player::UpdateOwnDerivedStatsTail(own, random);

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
