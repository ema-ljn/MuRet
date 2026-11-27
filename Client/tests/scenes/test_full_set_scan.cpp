#include "Actors/FullSetScan.h"

#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Kernel/CharacterAppearance.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// Client::Actors::ScanFullSet: 0x505820, checked against PE c32bbd5f (06, "El conjunto completo de 0x505820"). The
// values come from the ASM; none from a run.
namespace
{
using Client::Actors::FullSetScan;
using Client::Actors::ScannedCharacter;
using Client::Crt::CrtRandom;
using Client::Kernel::OwnCharacterRecord;
using Client::Models::OwnRecordCloseRandoms;
using Client::Tables::ItemModel;
using Client::Tests::AfterDraws;

constexpr std::uint32_t Seed = 0x505820;
constexpr std::uint8_t MagicGladiator = 3;
constexpr std::size_t Helm = 0;
constexpr std::size_t Armor = 1;
constexpr std::size_t Boots = 4;
// The own record's armor: slot 3 (+0xc44).
constexpr std::size_t OwnArmorSlot = 3;

// From the helmet to the boots, index `index` of groups 7 to 11, with levels `levels`.
ScannedCharacter Wearing(std::uint8_t characterClass, std::uint16_t index, const std::array<std::uint8_t, 5>& levels)
{
    ScannedCharacter character{.characterClass = characterClass};
    for (std::size_t part = 0; part < character.body.size(); ++part)
    {
        character.body[part] = {.model = ItemModel(static_cast<std::uint8_t>(7 + part), index), .level = levels[part]};
    }
    return character;
}

ScannedCharacter WearingPlus13(std::uint8_t characterClass, std::uint16_t index)
{
    return Wearing(characterClass, index, {13, 13, 13, 13, 13});
}

class FullSetScanTest : public ::testing::Test
{
protected:
    FullSetScan Scan(const ScannedCharacter& character)
    {
        return Client::Actors::ScanFullSet(character, m_own, m_accesses, m_random);
    }

    OwnCharacterRecord m_own;
    std::uint8_t m_accesses = 0;
    CrtRandom m_random{Seed};
};

// 0x50582c..0x5058f4: the lowest level of the five, which go from 9 to 15 with the same index as the boots; [0x743a17c]
// at 1 without opening the record.
TEST_F(FullSetScanTest, AFullSetLeavesItsLowestLevel)
{
    const FullSetScan scan = Scan(Wearing(1, 5, {12, 9, 15, 10, 11}));
    EXPECT_TRUE(scan.complete);
    EXPECT_EQ(scan.globals.dword743a178, 9);
    EXPECT_EQ(scan.globals.byte743a17c, 1);
    EXPECT_EQ(m_accesses, 0);
    EXPECT_EQ(m_random.State(), CrtRandom(Seed).State());

    // With all five equal, that one.
    EXPECT_EQ(Scan(Wearing(1, 5, {14, 14, 14, 14, 14})).globals.dword743a178, 14);
}

// 0x50582c..0x505846: every class looks at the helmet except those with & 7 at 3, which start with the armor.
TEST_F(FullSetScanTest, OnlyClassThreeSkipsTheHelm)
{
    for (std::uint8_t characterClass = 0; characterClass < 16; ++characterClass)
    {
        SCOPED_TRACE(int{characterClass});
        const bool skipsTheHelm = (characterClass & 7) == MagicGladiator;
        // The level 9 helmet: with it, the lowest is 9; without it, 13.
        const FullSetScan low = Scan(Wearing(characterClass, 5, {9, 13, 13, 13, 13}));
        EXPECT_TRUE(low.complete);
        EXPECT_EQ(low.globals.dword743a178, skipsTheHelm ? 13 : 9);

        // Without a helmet, or with a level 8 one or one of another index, only class 3 has the set.
        for (const std::uint16_t helm : {std::uint16_t{0xffff}, ItemModel(7, 6)})
        {
            ScannedCharacter character = WearingPlus13(characterClass, 5);
            character.body[Helm].model = helm;
            EXPECT_EQ(Scan(character).complete, skipsTheHelm);
        }
        ScannedCharacter lowHelm = WearingPlus13(characterClass, 5);
        lowHelm.body[Helm].level = 8;
        EXPECT_EQ(Scan(lowHelm).complete, skipsTheHelm);
    }
}

// 0x50585f: a part at 0xffff ends the first loop, before looking at the levels: [0x743a178] at 0.
TEST_F(FullSetScanTest, AnEmptyPartIsNoSet)
{
    for (std::size_t part = Armor; part <= Boots; ++part)
    {
        SCOPED_TRACE(part);
        for (const std::uint8_t characterClass : {std::uint8_t{0}, MagicGladiator})
        {
            ScannedCharacter character = WearingPlus13(characterClass, 5);
            character.body[part].model = 0xffff;
            const FullSetScan scan = Scan(character);
            EXPECT_FALSE(scan.complete);
            EXPECT_EQ(scan.globals.dword743a178, 0);
            EXPECT_EQ(scan.globals.byte743a17c, 1);
        }
    }
    EXPECT_EQ(m_accesses, 0);
    EXPECT_EQ(m_random.State(), CrtRandom(Seed).State());
}

// 0x5058ab..0x5058b4: the level is the byte & 0xf, and below 9, signed, it stops. The higher bits do not count: 0x19 is
// a 9 and 0xf8 an 8.
TEST_F(FullSetScanTest, ALevelBelowNineIsNoSetAndOnlyTheLowNibbleCounts)
{
    for (std::size_t part = 0; part <= Boots; ++part)
    {
        SCOPED_TRACE(part);
        ScannedCharacter character = WearingPlus13(0, 5);
        character.body[part].level = 8;
        const FullSetScan scan = Scan(character);
        EXPECT_FALSE(scan.complete);
        EXPECT_EQ(scan.globals.dword743a178, 0);
        EXPECT_EQ(scan.globals.byte743a17c, 1);
    }
    ScannedCharacter character = WearingPlus13(0, 5);
    character.body[Armor].level = 0x19;
    EXPECT_EQ(Scan(character).globals.dword743a178, 9);
    character.body[Armor].level = 0xf8;
    EXPECT_FALSE(Scan(character).complete);
}

// 0x505871..0x505893 and 0x5058b6..0x5058ce: the index is (signed model − 0x212) with the signed remainder of 512. The
// group does not matter; one of another index stops. With models below 0x212 the remainder is negative: 0x10 and 0x210
// both give −2, and 0x410 gives 510, which is not −2 even though it is 1,024 higher.
TEST_F(FullSetScanTest, TheIndexIsTheSignedRemainderOf512)
{
    for (std::size_t part = 0; part < Boots; ++part)
    {
        SCOPED_TRACE(part);
        ScannedCharacter character = WearingPlus13(0, 5);
        character.body[part].model = ItemModel(7, 6);
        EXPECT_FALSE(Scan(character).complete);
        // Any group with the same index.
        character.body[part].model = ItemModel(13, 5);
        EXPECT_TRUE(Scan(character).complete);
    }

    ScannedCharacter low = WearingPlus13(0, 0);
    for (std::size_t part = 0; part <= Boots; ++part)
    {
        low.body[part].model = part % 2 == 0 ? std::uint16_t{0x10} : std::uint16_t{0x210};
    }
    EXPECT_TRUE(Scan(low).complete);
    low.body[Armor].model = 0x410;
    EXPECT_FALSE(Scan(low).complete);
}

// 0x505902..0x505ba3: with class 3 and the set it opens the own record, looks at its armor (+0xc44) and closes it:
// [0x743a17c] stays at 1 only with 0x100f, 0x1014, 0x1017 or 0x1020. The close that leaves the count at 0 draws 3,848
// rand().
TEST_F(FullSetScanTest, TheMagicGladiatorKeepsTheBonusOnlyWithFourArmors)
{
    for (const std::uint16_t armor :
         {std::uint16_t{0x100f}, std::uint16_t{0x1014}, std::uint16_t{0x1017}, std::uint16_t{0x1020},
          std::uint16_t{0x1005}, std::uint16_t{0x0e0f}, std::uint16_t{0x200f}, std::uint16_t{0xffff}})
    {
        SCOPED_TRACE(armor);
        m_random = CrtRandom(Seed);
        m_own.equipment[OwnArmorSlot].id = armor;
        const bool keeps = armor == 0x100f || armor == 0x1014 || armor == 0x1017 || armor == 0x1020;
        // What it wears does not matter: only the record.
        const FullSetScan scan = Scan(WearingPlus13(MagicGladiator, 9));
        EXPECT_TRUE(scan.complete);
        EXPECT_EQ(scan.globals.dword743a178, 13);
        EXPECT_EQ(scan.globals.byte743a17c, keeps ? 1 : 0);
        EXPECT_EQ(m_accesses, 0);
        EXPECT_EQ(m_random.State(), AfterDraws(Seed, OwnRecordCloseRandoms).State());
    }
    // Class 11 is a 3.
    m_own.equipment[OwnArmorSlot].id = 0x1005;
    EXPECT_EQ(Scan(WearingPlus13(MagicGladiator + 8, 9)).globals.byte743a17c, 0);
}

// Without the set, or with another class, it does not open the record: [0x743a17c] at 1 and no rand(). With the record
// already open, the close does not leave it at 0 and draws nothing either.
TEST_F(FullSetScanTest, TheRecordOpensOnlyForAFullSetOfClassThree)
{
    m_own.equipment[OwnArmorSlot].id = 0x1005;
    ScannedCharacter broken = WearingPlus13(MagicGladiator, 9);
    broken.body[Boots].level = 7;
    EXPECT_EQ(Scan(broken).globals.byte743a17c, 1);
    EXPECT_EQ(Scan(WearingPlus13(2, 9)).globals.byte743a17c, 1);
    EXPECT_EQ(m_random.State(), CrtRandom(Seed).State());

    m_accesses = 1;
    EXPECT_EQ(Scan(WearingPlus13(MagicGladiator, 9)).globals.byte743a17c, 0);
    EXPECT_EQ(m_accesses, 1);
    EXPECT_EQ(m_random.State(), CrtRandom(Seed).State());
}

// What it reads from a character: the class, and the model and level from +0x218 to +0x298, every 0x20.
TEST(FullSetScanCharacterTest, ItReadsTheClassAndTheFiveBodyParts)
{
    Client::Kernel::ActorState actor = Client::Tests::UnarmedPlayer(0x23);
    Client::Kernel::CharacterAppearance appearance;
    appearance.characterClass = 0x23;
    for (std::size_t part = 0; part < actor.body.size(); ++part)
    {
        actor.body[part].model = static_cast<std::uint16_t>(0x1000 + part);
        actor.body[part].level = static_cast<std::uint8_t>(0x10 + part);
        appearance.bodyParts[part].model = static_cast<std::uint16_t>(0x2000 + part);
        appearance.bodyParts[part].level = static_cast<std::uint8_t>(0x20 + part);
    }

    const ScannedCharacter fromActor = Client::Actors::ScannedCharacterOf(actor);
    const ScannedCharacter fromAppearance = Client::Actors::ScannedCharacterOf(appearance);

    EXPECT_EQ(fromActor.characterClass, 0x23);
    EXPECT_EQ(fromAppearance.characterClass, 0x23);
    for (std::size_t part = 0; part < fromActor.body.size(); ++part)
    {
        SCOPED_TRACE(part);
        EXPECT_EQ(fromActor.body[part].model, actor.body[part].model);
        EXPECT_EQ(fromActor.body[part].level, actor.body[part].level);
        EXPECT_EQ(fromAppearance.body[part].model, appearance.bodyParts[part].model);
        EXPECT_EQ(fromAppearance.body[part].level, appearance.bodyParts[part].level);
    }
}

// 0x50c904..0x50c926 once per frame of the original: leaves the last one's result in WorldRuntime::fullSetScan, even if
// MuRet did not know what was there, and class 3 with the set opens and closes WorldState's record in each one, and
// returns the last one's result. Without frames, it returns the same without drawing or publishing anything.
TEST(FullSetScanDrawingTest, EachOriginalFrameScansTheDrawnCharacter)
{
    Client::Kernel::WorldState world;
    world.Runtime().fullSetScan = std::nullopt;
    world.OwnCharacter().equipment[OwnArmorSlot].id = 0x1005;
    CrtRandom random(Seed);

    const auto projected = Client::Actors::ScanTheDrawnCharacter(world, WearingPlus13(MagicGladiator, 9), random, 0);
    EXPECT_FALSE(world.Runtime().fullSetScan.has_value());
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());
    EXPECT_TRUE(projected.complete);
    EXPECT_EQ(projected.globals.dword743a178, 13);
    EXPECT_EQ(projected.globals.byte743a17c, 0);

    const auto scanned = Client::Actors::ScanTheDrawnCharacter(world, WearingPlus13(MagicGladiator, 9), random, 2);
    EXPECT_TRUE(scanned.complete);
    EXPECT_EQ(scanned.globals.dword743a178, 13);
    ASSERT_TRUE(world.Runtime().fullSetScan.has_value());
    EXPECT_EQ(world.Runtime().fullSetScan->dword743a178, 13);
    EXPECT_EQ(world.Runtime().fullSetScan->byte743a17c, 0);
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(random.State(), AfterDraws(Seed, 2 * OwnRecordCloseRandoms).State());

    // The next one overwrites both.
    EXPECT_FALSE(Client::Actors::ScanTheDrawnCharacter(world, Wearing(0, 9, {13, 13, 8, 13, 13}), random, 1).complete);
    EXPECT_EQ(world.Runtime().fullSetScan->dword743a178, 0);
    EXPECT_EQ(world.Runtime().fullSetScan->byte743a17c, 1);
}
} // namespace
