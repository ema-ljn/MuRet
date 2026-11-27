#include "Player/OwnDamage.h"

#include "Crt/CrtRandom.h"
#include "Kernel/CharacterClass.h"
#include "Tables/SkillTable.h"
#include "Models/LogicalAccess.h"
#include "Player/ItemSetOptionList.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Player/OwnStatHelpers.h"
#include "Kernel/ItemState.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

// The own character's damages in the character window (CL8 milestone 5; 06 § La ventana del personaje): 0x5b8b80's
// attack, 0x5513d0's magic and 0x5516e0's skill damage (the skill's tooltip), with their accesses and their rand()
// calls. The values come from the ASM checked against the PE c32bbd5f; the record, the sets' list and Skill.bmd are
// made up.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::CharacterClass;
using Client::Kernel::OwnCharacterRecord;
using Client::Models::OwnRecordCloseRandoms;
using Client::Player::AttackDamageOf;
using Client::Player::ItemSetOptionList;
using Client::Player::MagicDamageOf;
using Client::Player::OwnAttackDamage;
using Client::Player::OwnEquipmentOverride;
using Client::Player::SkillDamageOf;

constexpr std::uint32_t Seed = 0x2468aceu;
constexpr CharacterClass WizardClass = CharacterClass::DarkWizard;
constexpr CharacterClass KnightClass = CharacterClass::DarkKnight;
constexpr CharacterClass ElfClass = CharacterClass::Elf;
constexpr std::uint16_t EmptyType = 0xffff;

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

// The record with the attack's two pairs and empty hands.
OwnCharacterRecord Record()
{
    OwnCharacterRecord own;
    for (Client::Kernel::ItemState& item : own.equipment)
        item.id = EmptyType;
    own.word50 = 100;
    own.word52 = 150;
    own.word54 = 30;
    own.word56 = 45;
    return own;
}

OwnAttackDamage Attack(const OwnCharacterRecord& own, CharacterClass heroClass, const OwnEquipmentOverride& held = {})
{
    std::uint8_t depth = 0;
    CrtRandom random{Seed};
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms);
    const OwnAttackDamage damage = AttackDamageOf(own, heroClass, held, depth, random);
    EXPECT_EQ(depth, 0);
    EXPECT_EQ(random.State(), expected.State());
    return damage;
}

// 0x5b8cb2..0x5b8d32: with nothing on the right, the second pair; with a bow of the first range, the first; with the
// left from 0x800 to 0x806 and the right outside the ranges, the second. Each call closes the record.
TEST(OwnDamageTest, TheHandsChooseThePairOfTheAttack)
{
    OwnCharacterRecord own = Record();
    OwnAttackDamage damage = Attack(own, WizardClass);
    EXPECT_EQ(damage.min, 30u);
    EXPECT_EQ(damage.max, 45u);
    EXPECT_FALSE(damage.highlighted);

    own.equipment[0].id = 0x808;
    damage = Attack(own, WizardClass);
    EXPECT_EQ(damage.min, 100u);
    EXPECT_EQ(damage.max, 150u);

    own.equipment[0].id = 0x0003;
    own.equipment[1].id = 0x0805;
    damage = Attack(own, WizardClass);
    EXPECT_EQ(damage.min, 30u);

    own.equipment[1].id = 0x0807;
    damage = Attack(own, WizardClass);
    EXPECT_EQ(damage.min, 100u);
}

// Classes 1 and 3 with a weapon in each hand: 55 % of each pair, unsigned and truncating, and the line in orange. Bit
// 0x02 of +0x3c adds 15 to both.
TEST(OwnDamageTest, TwoWeaponsTakeFiftyFivePercentOfEachPair)
{
    OwnCharacterRecord own = Record();
    own.equipment[0].id = 0x0001;
    own.equipment[1].id = 0x0402;
    own.speedAdjustBits = 0x02;

    const OwnAttackDamage damage = Attack(own, KnightClass);

    EXPECT_TRUE(damage.highlighted);
    EXPECT_EQ(damage.min, 100u * 55u / 100u + 30u * 55u / 100u + 15u);
    EXPECT_EQ(damage.max, 150u * 55u / 100u + 45u * 55u / 100u + 15u);
}

// Class 2 with both hands from 0x800 to 0x9ff: the right's 0x80f or the left's 0x807 with a level (+4 & 0x78 at 8 or
// more) turns the orange on, without changing the damage.
TEST(OwnDamageTest, TheElfLightsTheLineWithLeveledArrowsOrBolts)
{
    OwnCharacterRecord own = Record();
    own.equipment[0].id = 0x80f;
    own.equipment[1].id = 0x0803;
    EXPECT_FALSE(Attack(own, ElfClass).highlighted);
    own.equipment[0].scalar1Low = 0x08;
    EXPECT_TRUE(Attack(own, ElfClass).highlighted);
    EXPECT_FALSE(Attack(own, WizardClass).highlighted);

    own = Record();
    own.equipment[0].id = 0x0804;
    own.equipment[1].id = 0x807;
    own.equipment[1].scalar1Low = 0x07;
    EXPECT_FALSE(Attack(own, ElfClass).highlighted);
    own.equipment[1].scalar1Low = 0x10;
    EXPECT_TRUE(Attack(own, ElfClass).highlighted);
}

// The cursor item ([0x7879f80]) takes the place of [0x78a6758]'s hand with [0x7879fbc] > 0 and the inventory grid;
// with the record already open, the closing draws nothing.
TEST(OwnDamageTest, TheCursorItemReplacesAHandAndANestedAccessSpendsNothing)
{
    OwnCharacterRecord own = Record();
    OwnEquipmentOverride held{.dword7879fbc = 1, .window78ab9b8Is78a9bb0 = true, .slot78a6758 = 0, .item7879f80 = {}};
    held.item7879f80.id = 0x808;
    EXPECT_EQ(Attack(own, WizardClass, held).min, 100u);
    held.window78ab9b8Is78a9bb0 = false;
    EXPECT_EQ(Attack(own, WizardClass, held).min, 30u);

    std::uint8_t depth = 1;
    CrtRandom random{Seed};
    const CrtRandom before = random;
    static_cast<void>(AttackDamageOf(own, WizardClass, held, depth, random));
    EXPECT_EQ(depth, 1);
    EXPECT_EQ(random.State(), before.State());
}

// A made-up Skill.bmd: skill `skill` with its bytes +0x21 and +0x30.
Client::Tables::SkillTable SkillsWith(std::uint8_t skill, std::uint8_t damage, std::uint8_t setSelector)
{
    std::vector<std::byte> plain(Client::Tables::SkillTableSize);
    const std::size_t base = std::size_t{skill} * Client::Tables::SkillRecordSize;
    plain[base + 0x21] = std::byte{damage};
    plain[base + 0x30] = std::byte{setSelector};
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Tables::SkillRecordSize);
    return Client::Tables::SkillTable(std::span<const std::byte>(encrypted));
}

// 0x5513d0: +0x5a + s and +0x5c + s + s / 2, plus p, which joins 0x40d8c0's sum (32 bits) and 0x40d690's with selector
// 0x9f on its low 16 bits. The marks start at 0, and the skill's entry draws 60 rand() calls on closing.
TEST(OwnDamageTest, TheMagicAddsTheSkillAndTheSetOptions)
{
    OwnCharacterRecord own = Record();
    own.word5a = 10;
    own.word5c = 20;
    const auto skills = SkillsWith(5, 31, 3);
    ItemSetOptionList list;
    list.count9348 = 3;
    list.entries[0] = {0x20, 1, 0};
    list.values[0] = 7;
    list.entries[1] = {0x13, 0, 0};
    list.values[1] = 70000;
    list.entries[2] = {0x20, 1, 0};
    list.values[2] = 2;
    list.used[2] = 1;
    std::uint8_t depth = 0;
    CrtRandom random{Seed};
    const CrtRandom expected = Advanced(random, static_cast<int>(Client::Tables::SkillRecordSize));

    const auto damage = MagicDamageOf(own, 5, &skills, depth, random, list);

    ASSERT_TRUE(damage.HasValue()) << damage.Error();
    // p = 7 + 2 by 0x40d8c0 (the third's mark was cleared before) and its low 16 bits plus 70,000.
    const std::int32_t bonus = (7 + 2 + 70000) & 0xffff;
    EXPECT_EQ(damage.Value().min, 10 + 31 + bonus);
    EXPECT_EQ(damage.Value().max, 20 + 31 + 15 + bonus);
    EXPECT_EQ(list.used[0], 1);
    EXPECT_EQ(list.used[1], 1);
    EXPECT_EQ(list.used[2], 1);
    EXPECT_EQ(depth, 0);
    EXPECT_EQ(random.State(), expected.State());
}

// Without Skill.bmd or the skill, an error before opening the entry; with more than 16 entries in the list, 0x40d8c0's.
TEST(OwnDamageTest, TheMagicStopsWithoutItsTables)
{
    const OwnCharacterRecord own = Record();
    ItemSetOptionList list;
    std::uint8_t depth = 0;
    CrtRandom random{Seed};
    const CrtRandom before = random;

    const auto missing = MagicDamageOf(own, 5, nullptr, depth, random, list);
    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("0x5513d0"), std::string::npos);
    EXPECT_EQ(random.State(), before.State());

    const auto skills = SkillsWith(5, 1, 0);
    list.count9348 = 17;
    const auto overflow = MagicDamageOf(own, 5, &skills, depth, random, list);
    ASSERT_FALSE(overflow.HasValue());
    EXPECT_NE(overflow.Error().find("0x40d8c0"), std::string::npos);
    EXPECT_EQ(depth, 0);
}

// 0x5516e0: the magic without the record's two words, s and s + s / 2 plus the same p. The entry opened outside draws
// nothing on closing.
TEST(OwnDamageTest, TheSkillDamageIsTheMagicWithoutTheRecord)
{
    const auto skills = SkillsWith(9, 45, 3);
    ItemSetOptionList list;
    list.count9348 = 2;
    list.entries[0] = {0x20, 1, 0};
    list.values[0] = 4;
    list.entries[1] = {0x13, 0, 0};
    list.values[1] = 65540;
    list.used[0] = 1;
    std::uint8_t depth = 0;
    CrtRandom random{Seed};
    const CrtRandom expected = Advanced(random, static_cast<int>(Client::Tables::SkillRecordSize));

    const auto damage = SkillDamageOf(9, &skills, depth, random, list);

    ASSERT_TRUE(damage.HasValue()) << damage.Error();
    const std::int32_t bonus = (4 + 65540) & 0xffff;
    EXPECT_EQ(damage.Value().min, 45 + bonus);
    EXPECT_EQ(damage.Value().max, 45 + 22 + bonus);
    EXPECT_EQ(list.used[0], 1);
    EXPECT_EQ(list.used[1], 1);
    EXPECT_EQ(depth, 0);
    EXPECT_EQ(random.State(), expected.State());

    depth = 1;
    const CrtRandom before = random;
    ASSERT_TRUE(SkillDamageOf(9, &skills, depth, random, list).HasValue());
    EXPECT_EQ(depth, 1);
    EXPECT_EQ(random.State(), before.State());
}

// Without Skill.bmd, an error before opening; with more than 16 entries in the list, 0x40d8c0's.
TEST(OwnDamageTest, TheSkillDamageStopsWithoutItsTables)
{
    ItemSetOptionList list;
    std::uint8_t depth = 0;
    CrtRandom random{Seed};
    const CrtRandom before = random;

    const auto missing = SkillDamageOf(9, nullptr, depth, random, list);
    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("0x5516e0"), std::string::npos);
    EXPECT_EQ(random.State(), before.State());

    const auto skills = SkillsWith(9, 1, 0);
    list.count9348 = 17;
    const auto overflow = SkillDamageOf(9, &skills, depth, random, list);
    ASSERT_FALSE(overflow.HasValue());
    EXPECT_NE(overflow.Error().find("0x40d8c0"), std::string::npos);
    EXPECT_EQ(depth, 0);
}
} // namespace
