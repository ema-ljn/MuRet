#include "Player/OwnStatHelpers.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemTable.h"
#include "Models/LogicalAccess.h"
#include "Actors/FullSetScan.h"
#include "Player/ItemSetOptionList.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/ItemState.h"
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

// The eight helpers 0x550d10 calls at 0x551287..0x5512ba, and 0x54ee00 (06, "Los ocho helpers de los stats propios").
// The values come from the ASM checked against the PE c32bbd5f and from the CRT's model for the rand() calls; these
// tests' Item.bmd is synthetic (the real one, in test_golden_own_stat_helpers.cpp).
namespace
{
using Client::Actors::Scan505820;
using Client::Crt::CrtRandom;
using Client::Kernel::ItemState;
using Client::Kernel::OwnCharacterRecord;
using Client::Models::OwnRecordCloseRandoms;
using Client::Player::ItemDurabilityFactor;
using Client::Player::ItemSetOptionList;
using Client::Player::OwnEquipmentOverride;
using Client::Tables::ItemTable;
using Client::Tests::AfterDraws;

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

// The Item.bmd type: 16 sections of 512.
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

// A synthetic Item.bmd: byte +0x2d of each type in `bases` and everything else at 0.
ItemTable ItemsWithBaseDurability(std::initializer_list<BaseDurability> bases)
{
    std::vector<std::byte> plain(Client::Tables::ItemTableSize);
    for (const BaseDurability& base : bases)
    {
        plain[base.type * Client::Tables::ItemRecordSize + BaseDurabilityOffset] = std::byte{base.value};
    }
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Tables::ItemRecordSize);
    return ItemTable(std::span<const std::byte>(encrypted));
}

struct Option
{
    std::uint8_t tag = 0;
    std::uint8_t value = 0;
};

// An item of the given type and durability, with its options (D+0x2b the tag and D+0x33 the value).
ItemState Item(std::uint16_t type, std::uint8_t durability, std::initializer_list<Option> options = {})
{
    ItemState item;
    item.id = type;
    item.durability = durability;
    std::size_t index = 0;
    for (const Option& option : options)
    {
        item.optionBytes[index] = option.tag;
        item.optionBytes[Client::Kernel::ItemOptionValueOffset + index] = option.value;
        ++index;
    }
    item.optionCount = static_cast<std::uint8_t>(index);
    return item;
}

// A record of the given class with the twelve places empty (type −1).
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

// [0x6eed3c]'s list with the entries in +0x9348 and the marks at 0.
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

// r = 1 − durability / maximum, in double, against 0.8f, 0.7f and 0.5f. 40 of 200 leaves 0.8 in double, which does
// not pass 0.8f; 6 of 20 leaves 0.7 in double, which passes 0.7f (with PC24 the quotient would be 0.3f and not pass).
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

// The maximum adds 1 for each of the first 4 levels and 2 for the others; 20 with D+0x1c % 4 at 1 or 2 or, otherwise,
// 15 with D+0x1b & 0x3f. With the maximum at 0 no comparison passes.
TEST(OwnStatHelpersTest, TheMaximumDurabilityGrowsWithTheLevelAndTheFlags)
{
    EXPECT_EQ(ItemDurabilityFactor(6, 14, 0, 0, 0), 0.2f);
    EXPECT_EQ(ItemDurabilityFactor(6, 14, 5 << 3, 0, 0), 0.3f); // 14 + 4 + 2 = 20
    EXPECT_EQ(ItemDurabilityFactor(6, 16, 4 << 3, 0, 0), 0.3f); // 16 + 4 = 20
    EXPECT_EQ(ItemDurabilityFactor(6, 0, 0x1ff, 0, 0), 0.3f);   // level 15: 4 + 22 = 26

    EXPECT_EQ(ItemDurabilityFactor(6, 0, 0, 0, 1), 0.3f);
    EXPECT_EQ(ItemDurabilityFactor(6, 0, 0, 0, 0xfe), 0.3f);
    EXPECT_EQ(ItemDurabilityFactor(6, 0, 0, 0, 3), 0.0f);
    EXPECT_EQ(ItemDurabilityFactor(6, 0, 0, 0x01, 0), 0.2f); // 15
    EXPECT_EQ(ItemDurabilityFactor(6, 0, 0, 0x40, 0), 0.0f);
    EXPECT_EQ(ItemDurabilityFactor(6, 0, 0, 0x01, 1), 0.3f); // 20 and not 35, which would give 0.5f

    EXPECT_EQ(ItemDurabilityFactor(0, 0, 0, 0, 0), 0.0f);
    EXPECT_EQ(ItemDurabilityFactor(5, 0, 0, 0, 0), 0.0f);
}

// ---- 0x54efe0 ----

// S = +0x2a + strength, A = +0x2c + agility and E = +0x30 + energy; +0x54 and +0x56 copy +0x50 and +0x52.
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

        ASSERT_TRUE(Client::Player::UpdateOwnWords50To56(own, {}, nullptr, &setOptions).HasValue());

        EXPECT_EQ(own.word50, expected.word50);
        EXPECT_EQ(own.word52, expected.word52);
        EXPECT_EQ(own.word54, expected.word50);
        EXPECT_EQ(own.word56, expected.word52);
    }
}

// A crossbow on the right or a bow on the left, with durability: S/14 + A/7 and A/4 + S/8, in any class. A level 2
// Bolt on the left: w + _ftol(w · 5 · 0.01f + 1), which with the float gives 168 and 294 (with an exact 0.05 they
// would be 169 and 295), and does not touch +0x54 or +0x56. A level 1 Arrow on the right, those two.
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
    ASSERT_TRUE(Client::Player::UpdateOwnWords50To56(own, {}, &items, &setOptions).HasValue());
    EXPECT_EQ(own.word50, 168);
    EXPECT_EQ(own.word52, 294);
    EXPECT_EQ(own.word54, 160);
    EXPECT_EQ(own.word56, 280);

    ItemState arrow = Item(Arrow, 1);
    arrow.scalar1Low = 1 << 3;
    own = archer(arrow, Item(ShortBow, 1));
    ASSERT_TRUE(Client::Player::UpdateOwnWords50To56(own, {}, &items, &setOptions).HasValue());
    EXPECT_EQ(own.word50, 160);
    EXPECT_EQ(own.word52, 280);
    EXPECT_EQ(own.word54, 165);
    EXPECT_EQ(own.word56, 289);

    // Without a level, nothing; a Bolt alone is not the branch, nor a crossbow without durability: the Elf's.
    own = archer(Item(Crossbow, 1), Item(Bolt, 1));
    ASSERT_TRUE(Client::Player::UpdateOwnWords50To56(own, {}, &items, &setOptions).HasValue());
    EXPECT_EQ(own.word50, 160);
    EXPECT_EQ(own.word52, 280);
    own = archer(Item(EmptyType, 0), bolt);
    ASSERT_TRUE(Client::Player::UpdateOwnWords50To56(own, {}, &items, &setOptions).HasValue());
    EXPECT_EQ(own.word50, 180);
    EXPECT_EQ(own.word52, 315);
    own = archer(Item(Crossbow, 0), Item(EmptyType, 0));
    ASSERT_TRUE(Client::Player::UpdateOwnWords50To56(own, {}, &items, &setOptions).HasValue());
    EXPECT_EQ(own.word50, 180);
    EXPECT_EQ(own.word52, 315);
}

// A DK with S = 120 (20 and 30). The wings (+0x2d at 200, 30 of durability: 0.5f) add their 0x50 (9 − 4 = 5) to the
// four. The right (Kris, 2 of 22: 0.5f) adds D+0xc and D+0xe plus its reduced 0x50 (11 and 17) to +0x50 and +0x52,
// and 0x5d (4) and 0x5e by 2 to those two. The shield (factor 0) adds 10 and 6 to +0x54 and +0x56 and its 0x5d. Place
// 9 adds its 0x5d to the four and place 10, 0x5e by 10; place 11, without durability, does not.
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

    ASSERT_TRUE(Client::Player::UpdateOwnWords50To56(own, {}, &items, &setOptions).HasValue());

    // 25 + 11 + 4 = 40 (plus 0), + 3 = 43, + 4 = 47; 35 + 17 + 4 = 56, + 1 = 57, + 3 = 60, + 6 = 66;
    // 25 + 10 + 1 + 3 = 39, + 3 = 42; 35 + 6 + 1 + 3 = 45, + 4 = 49.
    EXPECT_EQ(own.word50, 47);
    EXPECT_EQ(own.word52, 66);
    EXPECT_EQ(own.word54, 42);
    EXPECT_EQ(own.word56, 49);
}

// A right hand from 0xa00 to 0xc00 (signed, both included) goes to +0x54 and +0x56; wings with +0x2d at 0 add nothing.
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

        ASSERT_TRUE(Client::Player::UpdateOwnWords50To56(own, {}, &items, &setOptions).HasValue());

        EXPECT_EQ(own.word50, right.secondPair ? 0 : 7);
        EXPECT_EQ(own.word52, right.secondPair ? 0 : 9);
        EXPECT_EQ(own.word54, right.secondPair ? 7 : 0);
        EXPECT_EQ(own.word56, right.secondPair ? 9 : 0);
    }
}

// 0x91 to +0x54 and +0x50, 0x92 to +0x56 and +0x52, and A · 50 / 100 (0xa5) and S · 10 / 100 (0xa4) to the four.
TEST(OwnStatHelpersTest, Words50To56AddTheSetOptions)
{
    OwnCharacterRecord own = EmptyRecord(DarkWizard);
    own.strength = 120;
    own.agility = 84;
    ItemSetOptionList setOptions = SetOptionsOf({{0x05, 0, 7}, {0x06, 0, 11}, {0x19, 1, 50}, {0x18, 1, 10}});

    ASSERT_TRUE(Client::Player::UpdateOwnWords50To56(own, {}, nullptr, &setOptions).HasValue());

    EXPECT_EQ(own.word50, 15 + 7 + 42 + 12);
    EXPECT_EQ(own.word52, 30 + 11 + 42 + 12);
    EXPECT_EQ(own.word54, 15 + 7 + 42 + 12);
    EXPECT_EQ(own.word56, 30 + 11 + 42 + 12);
    EXPECT_EQ(setOptions.used, (std::array<std::uint8_t, 16>{1, 1, 1, 1}));
}

// 0x7879f80's item replaces the right hand with [0x7879fbc] > 0, the window and place 0.
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

    ASSERT_TRUE(Client::Player::UpdateOwnWords50To56(own, held, &items, &setOptions).HasValue());
    EXPECT_EQ(own.word50, 0);
    EXPECT_EQ(own.word54, 7);

    for (const std::int32_t count : {0, -1})
    {
        held.dword7879fbc = count;
        ASSERT_TRUE(Client::Player::UpdateOwnWords50To56(own, held, &items, &setOptions).HasValue());
        EXPECT_EQ(own.word50, 5);
        EXPECT_EQ(own.word54, 0);
    }
    held.dword7879fbc = 1;
    held.window78ab9b8Is78a9bb0 = false;
    ASSERT_TRUE(Client::Player::UpdateOwnWords50To56(own, held, &items, &setOptions).HasValue());
    EXPECT_EQ(own.word50, 5);
}

// What is missing comes out as an error where the original reads it, with what was written up to there.
TEST(OwnStatHelpersTest, Words50To56StopWhereTheyReadWhatMuRetLacks)
{
    OwnCharacterRecord own = EmptyRecord(DarkKnight);
    own.strength = 120;
    own.equipment[RightHand] = Item(Kris, 1);
    ItemSetOptionList setOptions;
    auto updated = Client::Player::UpdateOwnWords50To56(own, {}, nullptr, &setOptions);
    ASSERT_FALSE(updated.HasValue());
    EXPECT_NE(updated.Error().find("Item.bmd"), std::string::npos);
    EXPECT_EQ(own.word50, 20);

    const ItemTable items = ItemsWithBaseDurability({});
    updated = Client::Player::UpdateOwnWords50To56(own, {}, &items, nullptr);
    ASSERT_FALSE(updated.HasValue());
    EXPECT_NE(updated.Error().find("0x40e1b0"), std::string::npos);

    own.equipment[RightHand].optionCount = 10;
    updated = Client::Player::UpdateOwnWords50To56(own, {}, &items, &setOptions);
    ASSERT_FALSE(updated.HasValue());
    EXPECT_NE(updated.Error().find("10 opciones"), std::string::npos);
}

// ---- 0x54f9b0 ----

// E/9 and E/4 (E = +0x30 + energy). 0x40d690 adds to +0xef4 without zeroing it, and +0xef4 is left with 0x40d7f0's:
// the next call adds it again.
TEST(OwnStatHelpersTest, Words5aAnd5cCarryWordEf4IntoTheNextCall)
{
    OwnCharacterRecord own = EmptyRecord(DarkWizard);
    own.energy = 80;
    own.word30 = 10;
    own.wordef4 = 5;
    ItemSetOptionList setOptions = SetOptionsOf({{0x07, 0, 3}, {0x1c, 1, 10}});

    ASSERT_TRUE(Client::Player::UpdateOwnWords5aAnd5c(own, {}, nullptr, &setOptions).HasValue());
    EXPECT_EQ(own.word5a, 10 + 8 + 9);
    EXPECT_EQ(own.word5c, 22 + 8 + 9);
    EXPECT_EQ(own.wordef4, 9);

    // The entries are already marked: it adds the 9 that was left.
    ASSERT_TRUE(Client::Player::UpdateOwnWords5aAnd5c(own, {}, nullptr, &setOptions).HasValue());
    EXPECT_EQ(own.word5a, 10 + 9);
    EXPECT_EQ(own.word5c, 22 + 9);
    EXPECT_EQ(own.wordef4, 0);
}

// The right adds its reduced 0x51 (0x50 with 0 21, 0 23 and 0 31), 0x5f and 0x60 by 2. With E = 900: 100 and 225;
// 9 − 4 = 5 or 7 − 3 = 4; 6; and w · 2 / 100.
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

        ASSERT_TRUE(Client::Player::UpdateOwnWords5aAnd5c(own, {}, &items, &setOptions).HasValue());

        EXPECT_EQ(own.word5a, expected.word5a);
        EXPECT_EQ(own.word5c, expected.word5c);
    }
}

// The wings add their reduced 0x51 also with +0x2d at 0 (factor 0). Place 9: 0x5f and 0x60 by 2; place 10: 0x60 by
// 10.
TEST(OwnStatHelpersTest, Words5aAnd5cAddTheWingsAndTheAccessories)
{
    const ItemTable items = ItemsWithBaseDurability({{WingsOfElf, 200}});
    ItemSetOptionList setOptions;
    OwnCharacterRecord own = EmptyRecord(DarkWizard);
    own.equipment[Wings] = Item(WingsOfElf, 30, {{0x51, 9}});
    ASSERT_TRUE(Client::Player::UpdateOwnWords5aAnd5c(own, {}, &items, &setOptions).HasValue());
    EXPECT_EQ(own.word5a, 5);
    EXPECT_EQ(own.word5c, 5);

    own.equipment[Wings] = Item(TypeOf(12, 1), 30, {{0x51, 9}});
    ASSERT_TRUE(Client::Player::UpdateOwnWords5aAnd5c(own, {}, &items, &setOptions).HasValue());
    EXPECT_EQ(own.word5a, 9);
    EXPECT_EQ(own.word5c, 9);

    own = EmptyRecord(DarkWizard);
    own.energy = 900;
    own.equipment[Slot9] = Item(TypeOf(13, 12), 1, {{0x5f, 4}, {0x60, 0}});
    own.equipment[Slot10] = Item(TypeOf(13, 8), 1, {{0x60, 0}});
    ASSERT_TRUE(Client::Player::UpdateOwnWords5aAnd5c(own, {}, nullptr, &setOptions).HasValue());
    EXPECT_EQ(own.word5a, 116); // 104, 106, 116
    EXPECT_EQ(own.word5c, 256); // 229, 233, 256
}

TEST(OwnStatHelpersTest, Words5aAnd5cStopWhereTheyReadWhatMuRetLacks)
{
    OwnCharacterRecord own = EmptyRecord(DarkWizard);
    own.energy = 90;
    auto updated = Client::Player::UpdateOwnWords5aAnd5c(own, {}, nullptr, nullptr);
    ASSERT_FALSE(updated.HasValue());
    EXPECT_NE(updated.Error().find("0x40e1b0"), std::string::npos);
    EXPECT_EQ(own.word5a, 10);
    EXPECT_EQ(own.word5c, 22);

    own.equipment[Wings] = Item(WingsOfElf, 0);
    ItemSetOptionList setOptions;
    updated = Client::Player::UpdateOwnWords5aAnd5c(own, {}, nullptr, &setOptions);
    ASSERT_FALSE(updated.HasValue());
    EXPECT_NE(updated.Error().find("Item.bmd"), std::string::npos);
}

// ---- 0x54fd40 ----

// A · 3 / 2 + level · 5 + S / 4, and for the DL S/6 + L/10 + A · 5 / 2 + level · 5 (L = +0x36 + command); then
// 0x40d690 with 0x95. The level times 5 is 16 bits.
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
        own.strength = expected.word4e == 113 ? 55 : 25; // 60 or 30
        own.word2c = 5;
        own.agility = 15; // 20
        own.word36 = 5;
        own.leadership = 25; // 30
        own.level = 10;
        ItemSetOptionList setOptions = SetOptionsOf({{0x09, 0, 4}});

        ASSERT_TRUE(Client::Player::UpdateOwnWord4e(own, &setOptions).HasValue());

        EXPECT_EQ(own.word4e, expected.word4e + 4);
    }

    OwnCharacterRecord own = EmptyRecord(DarkWizard);
    own.level = 13108; // · 5 = 65540
    ItemSetOptionList setOptions;
    ASSERT_TRUE(Client::Player::UpdateOwnWord4e(own, &setOptions).HasValue());
    EXPECT_EQ(own.word4e, 4);

    own.level = 10;
    const auto updated = Client::Player::UpdateOwnWord4e(own, nullptr);
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

        ASSERT_TRUE(Client::Player::UpdateOwnAttackAndMagicSpeeds(own, {}, accesses, random).HasValue());

        EXPECT_EQ(own.attackSpeed, expected.attack);
        EXPECT_EQ(own.magicSpeed, expected.magic);
        EXPECT_EQ(random.State(), CrtRandom(7).State());
    }
}

// Each hand's D+0x17 (0 without durability) with a type from 0 to 0xbff except Bolt and Arrow; with both, the
// average. A right hand without durability counts as 0 in the average.
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

        ASSERT_TRUE(Client::Player::UpdateOwnAttackAndMagicSpeeds(own, {}, accesses, random).HasValue());

        EXPECT_EQ(own.attackSpeed, expected.added);
        EXPECT_EQ(own.magicSpeed, expected.added);
    }
}

// Wizard's Ring with durability and D+4 >> 3 (signed, without the level mask) at 0 or 3: 10 more and bit 0x04 of +0x3c
// set, between an opening and a closing of the record (3,848 rand() calls if it leaves it at 0).
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

        ASSERT_TRUE(Client::Player::UpdateOwnAttackAndMagicSpeeds(own, {}, accesses, random).HasValue());

        EXPECT_EQ(own.attackSpeed, ring.counts ? 10 : 0);
        EXPECT_EQ(own.magicSpeed, ring.counts ? 10 : 0);
        EXPECT_EQ(own.speedAdjustBits, ring.counts ? 0x04 : 0);
        EXPECT_EQ(random.State(), AfterDraws(Seed, ring.counts ? OwnRecordCloseRandoms : 0).State());
        EXPECT_EQ(accesses, 0);
    }

    // With another access open the closing does not leave it at 0 and draws nothing; the two rings add once.
    OwnCharacterRecord own = EmptyRecord(DarkWizard);
    own.equipment[Slot10] = Item(WizardsRing, 1);
    own.equipment[Slot11] = Item(WizardsRing, 1);
    std::uint8_t accesses = 1;
    CrtRandom random(Seed);
    ASSERT_TRUE(Client::Player::UpdateOwnAttackAndMagicSpeeds(own, {}, accesses, random).HasValue());
    EXPECT_EQ(own.attackSpeed, 10);
    EXPECT_EQ(accesses, 1);
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());
}

// Without a ring and with bit 0x04 set it clears it, in another access; with the bit clear it opens nothing.
TEST(OwnStatHelpersTest, WithoutTheRingBit04IsClearedInsideAnAccess)
{
    constexpr std::uint32_t Seed = 12;
    OwnCharacterRecord own = EmptyRecord(DarkWizard);
    own.speedAdjustBits = 0x06;
    std::uint8_t accesses = 0;
    CrtRandom random(Seed);

    ASSERT_TRUE(Client::Player::UpdateOwnAttackAndMagicSpeeds(own, {}, accesses, random).HasValue());
    EXPECT_EQ(own.speedAdjustBits, 0x02);
    EXPECT_EQ(random.State(), AfterDraws(Seed, OwnRecordCloseRandoms).State());

    random = CrtRandom(Seed);
    ASSERT_TRUE(Client::Player::UpdateOwnAttackAndMagicSpeeds(own, {}, accesses, random).HasValue());
    EXPECT_EQ(own.speedAdjustBits, 0x02);
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());
}

// A DK with A = 300 (20 and 15): the Kris (20), the ring (10), the gloves (7), bit 0x08 (20) and the 0x61 options of
// places 0 (5) and 8 (3); place 9, without durability, adds nothing.
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

    ASSERT_TRUE(Client::Player::UpdateOwnAttackAndMagicSpeeds(own, {}, accesses, random).HasValue());

    EXPECT_EQ(own.attackSpeed, 20 + 20 + 10 + 7 + 20 + 5 + 3);
    EXPECT_EQ(own.magicSpeed, 15 + 20 + 10 + 7 + 20 + 5 + 3);
    EXPECT_EQ(own.speedAdjustBits, 0x0c);

    // The gloves without durability or with type −1, and bit 0x01.
    own.speedAdjustBits = 0x01;
    own.equipment[Gloves].durability = 0;
    ASSERT_TRUE(Client::Player::UpdateOwnAttackAndMagicSpeeds(own, {}, accesses, random).HasValue());
    EXPECT_EQ(own.attackSpeed, 20 + 20 + 10 + 20 + 5 + 3);
    own.equipment[Gloves] = Item(EmptyType, 1);
    own.equipment[Gloves].byte17 = 7;
    ASSERT_TRUE(Client::Player::UpdateOwnAttackAndMagicSpeeds(own, {}, accesses, random).HasValue());
    EXPECT_EQ(own.attackSpeed, 20 + 20 + 10 + 20 + 5 + 3);
}

// 0x7879f80's item replaces the hands (and the gloves and the rings) but the 0x61 options are the record's.
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

    ASSERT_TRUE(Client::Player::UpdateOwnAttackAndMagicSpeeds(own, held, accesses, random).HasValue());
    EXPECT_EQ(own.attackSpeed, 15 + 40 + 5);
    EXPECT_EQ(own.magicSpeed, 30 + 40 + 5);

    held.slot78a6758 = 1;
    ASSERT_TRUE(Client::Player::UpdateOwnAttackAndMagicSpeeds(own, held, accesses, random).HasValue());
    EXPECT_EQ(own.attackSpeed, 15 + 30 + 5);

    held.slot78a6758 = 5;
    held.item7879f80.byte17 = 7;
    ASSERT_TRUE(Client::Player::UpdateOwnAttackAndMagicSpeeds(own, held, accesses, random).HasValue());
    EXPECT_EQ(own.attackSpeed, 15 + 20 + 7 + 5);

    held.slot78a6758 = 11;
    held.item7879f80 = Item(WizardsRing, 1);
    ASSERT_TRUE(Client::Player::UpdateOwnAttackAndMagicSpeeds(own, held, accesses, random).HasValue());
    EXPECT_EQ(own.attackSpeed, 15 + 20 + 10 + 5);
    EXPECT_EQ(own.speedAdjustBits, 0x04);

    own.equipment[Pet] = Item(TypeOf(13, 0), 1);
    own.equipment[Pet].optionCount = 10;
    const auto updated = Client::Player::UpdateOwnAttackAndMagicSpeeds(own, {}, accesses, random);
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

        ASSERT_TRUE(Client::Player::UpdateOwnWord60(own, nullptr).HasValue());

        EXPECT_EQ(own.word60, expected.word60);
    }
}

// Place 1 (2 of 22: 0.5f) adds its reduced D+0x10 (3 − 1) and its reduced 0x52 (9 − 4), and 0x5a by 10; then 0x5a by
// 10 of places 2 to 6, 11 and 10, also without durability, and not of 8 or 9.
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

    ASSERT_TRUE(Client::Player::UpdateOwnWord60(own, &items).HasValue());

    EXPECT_EQ(own.word60, 140); // 102, 107, 117, 128, 140

    own.equipment[LeftHand].optionCount = 10;
    auto updated = Client::Player::UpdateOwnWord60(own, &items);
    ASSERT_FALSE(updated.HasValue());
    EXPECT_EQ(own.word60, 102);

    updated = Client::Player::UpdateOwnWord60(own, nullptr);
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

        ASSERT_TRUE(Client::Player::UpdateOwnWord62(own, nullptr, {}, &setOptions).HasValue());

        EXPECT_EQ(own.word62, expected.word62);
    }
}

// Places 1 to 7 with durability: D+0x12 plus its 0x53, reduced. The helmet (3 of 34: 0.5f) 14 − 7; the boots, without
// wear, 7; the wings (40 of 200) with 0 and 0 instead of their D+0x1b and D+0x1c: 0.3f and 10 − 3 (with their D+0x1c
// at 1 they would be 0.5f and 5). The armor, without durability, nothing.
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

    ASSERT_TRUE(Client::Player::UpdateOwnWord62(own, &items, {}, &setOptions).HasValue());

    EXPECT_EQ(own.word62, 75 + 7 + 7 + 7);
}

// [0x743a17c] other than 0: w + _ftol(w · f), with 0.05f, 0.1f, 0.15f or 0.2f for [0x743a178] from 10 to 13 and 0 for
// the others (the subtraction of 10 wraps unsigned).
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

        ASSERT_TRUE(Client::Player::UpdateOwnWord62(own, nullptr, expected.scan, &setOptions).HasValue());

        EXPECT_EQ(own.word62, expected.word62);
    }
}

// 0x96 (5), A · 10 / 100 (0xa6) and V · 20 / 100 (0xa7) and, with a type from 0xc00 to 0xdff in place 1, 50 % of 0xa2.
// A DK with A = 30 (10) and V = 50.
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

        ASSERT_TRUE(Client::Player::UpdateOwnWord62(own, nullptr, {}, &setOptions).HasValue());

        EXPECT_EQ(own.word62, expected.word62);
    }
}

// Place 8's 0x96 options and, if the wings are Cape of Lord, theirs.
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

        ASSERT_TRUE(Client::Player::UpdateOwnWord62(own, &items, {}, &setOptions).HasValue());

        EXPECT_EQ(own.word62, wings == CapeOfLord ? 35 : 32);
    }
}

// A positive type outside the table can add and needs the read: an error with +0x62 at the base.
TEST(OwnStatHelpersTest, Word62StopsWhereItReadsWhatMuRetLacks)
{
    const ItemTable items = ItemsWithBaseDurability({});
    OwnCharacterRecord own = EmptyRecord(DarkWizard);
    own.agility = 300;
    own.equipment[Armor] = Item(0x2000, 5);
    ItemSetOptionList setOptions;

    auto updated = Client::Player::UpdateOwnWord62(own, &items, {}, &setOptions);
    ASSERT_FALSE(updated.HasValue());
    EXPECT_NE(updated.Error().find("Q-C184"), std::string::npos);
    EXPECT_EQ(own.word62, 75);

    own.equipment[Armor] = Item(TypeOf(8, 0), 5);
    updated = Client::Player::UpdateOwnWord62(own, nullptr, {}, &setOptions);
    ASSERT_FALSE(updated.HasValue());
    EXPECT_NE(updated.Error().find("Item.bmd"), std::string::npos);

    updated = Client::Player::UpdateOwnWord62(own, &items, {12, 1}, nullptr);
    ASSERT_FALSE(updated.HasValue());
    EXPECT_NE(updated.Error().find("0x40e1b0"), std::string::npos);
    EXPECT_EQ(own.word62, 75 + 11); // the armor adds 0 and 15 % of 75 is 11
}

// 0x54e686..0x54e68c returns AX=0 before looking at the options for IDffff. 0x550aee..0x550b1b reduces that 0 by a
// finite factor: it does not need the byte at base(Item.bmd)-39.
TEST(OwnStatHelpersTest, EmptySlotsWithDurabilityContributeZeroWithoutReadingResidualOptions)
{
    OwnCharacterRecord own = EmptyRecord(DarkWizard);
    own.agility = 300;
    for (std::size_t slot = LeftHand; slot <= Wings; ++slot)
    {
        own.equipment[slot] = Item(EmptyType, 25);
        own.equipment[slot].word12 = 99;
        own.equipment[slot].optionCount = 0xff;
    }
    ItemSetOptionList setOptions;

    const auto updated = Client::Player::UpdateOwnWord62(own, nullptr, {}, &setOptions);

    ASSERT_TRUE(updated.HasValue()) << updated.Error();
    EXPECT_EQ(own.word62, 75);
    EXPECT_EQ(own.equipment[Armor].id, EmptyType);
    EXPECT_EQ(own.equipment[Armor].durability, 25);
    EXPECT_EQ(own.equipment[Armor].word12, 99);
    EXPECT_EQ(own.equipment[Armor].optionCount, 0xff);
}

// ---- 0x550c30 and 0x550c60 ----

// +0x64 is the D+0x14 of the last of places 2 to 7 with durability (0 with type −1); with none, it stays.
TEST(OwnStatHelpersTest, Word64IsWord14OfTheLastBodySlotWithDurability)
{
    OwnCharacterRecord own = EmptyRecord(DarkWizard);
    own.word64 = 0x1234;
    own.equipment[RightHand] = Item(Kris, 1);
    own.equipment[RightHand].word14 = 99;
    own.equipment[Pet] = Item(TypeOf(13, 0), 1);
    own.equipment[Pet].word14 = 98;
    Client::Player::UpdateOwnWord64(own);
    EXPECT_EQ(own.word64, 0x1234);

    own.equipment[Helm] = Item(BronzeHelm, 1);
    own.equipment[Helm].word14 = 11;
    own.equipment[Armor] = Item(TypeOf(8, 0), 0);
    own.equipment[Armor].word14 = 22;
    own.equipment[Pants] = Item(TypeOf(9, 0), 3);
    own.equipment[Pants].word14 = 33;
    Client::Player::UpdateOwnWord64(own);
    EXPECT_EQ(own.word64, 33);

    own.equipment[Wings].durability = 1;
    Client::Player::UpdateOwnWord64(own);
    EXPECT_EQ(own.word64, 0);
}

// +0x66 is the boots' D+0x18 with durability plus the wings' with durability; without the boots, the wings' is added to
// what was there.
TEST(OwnStatHelpersTest, Word66IsWord18OfTheBootsPlusTheWings)
{
    OwnCharacterRecord own = EmptyRecord(DarkKnight);
    own.word66 = 1000;
    own.equipment[Boots] = Item(BronzeBoots, 2);
    own.equipment[Boots].word18 = 7;
    own.equipment[Wings] = Item(WingsOfElf, 1);
    own.equipment[Wings].word18 = 5;
    Client::Player::UpdateOwnWord66(own);
    EXPECT_EQ(own.word66, 12);

    own.equipment[Boots].durability = 0;
    Client::Player::UpdateOwnWord66(own);
    EXPECT_EQ(own.word66, 17);

    own.equipment[Boots] = Item(EmptyType, 4);
    own.equipment[Boots].word18 = 7;
    Client::Player::UpdateOwnWord66(own);
    EXPECT_EQ(own.word66, 5);

    own.equipment[Boots].durability = 0;
    own.equipment[Wings].durability = 0;
    Client::Player::UpdateOwnWord66(own);
    EXPECT_EQ(own.word66, 5);
}
} // namespace
