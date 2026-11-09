#include "Scenes/OwnCharacterRecord.h"

#include "Protocol/Character/CharacterSelectResult.h"
#include "World/ItemState.h"

#include <gtest/gtest.h>

#include <cstddef>

namespace
{
TEST(OwnCharacterRecordTest, EntryResetPreservesOccupancyAndOtherHistoricalFields)
{
    Client::Scenes::OwnCharacterRecord own;
    for (auto& item : own.equipment)
    {
        item.id = 0x1234;
        item.scalar1Low = 0x87654321;
        item.occupied = 1;
        item.flags1b = 0x91;
        item.flags1c = 0x82;
        item.durability = 0x73;
        item.word28 = 0x6453;
    }
    own.ClearEquipmentForEntry();
    for (const auto& item : own.equipment)
    {
        EXPECT_EQ(item.id, 0xffff);
        EXPECT_EQ(item.scalar1Low, 0u);
        EXPECT_EQ(item.occupied, 1u);
        EXPECT_EQ(item.flags1b, 0);
        EXPECT_EQ(item.flags1c, 0x82);
        EXPECT_EQ(item.durability, 0x73);
        EXPECT_EQ(item.word28, 0x6453);
    }
}

TEST(OwnCharacterRecordTest, InventoryResetPreservesTheScalarAndOtherHistoricalFields)
{
    Client::Scenes::OwnCharacterRecord own;
    for (auto& item : own.equipment)
    {
        item.id = 0x1234;
        item.scalar1Low = 0x87654321;
        item.occupied = 1;
        item.flags1b = 0x91;
        item.flags1c = 0x82;
        item.durability = 0x73;
        item.word28 = 0x6453;
    }
    own.ClearEquipmentForInventory();
    for (const auto& item : own.equipment)
    {
        EXPECT_EQ(item.id, 0xffff);
        EXPECT_EQ(item.scalar1Low, 0x87654321u);
        EXPECT_EQ(item.occupied, 0u);
        EXPECT_EQ(item.flags1b, 0);
        EXPECT_EQ(item.flags1c, 0x82);
        EXPECT_EQ(item.durability, 0x73);
        EXPECT_EQ(item.word28, 0x6453);
    }
}
TEST(OwnCharacterRecordTest, ReceivedStatsPreserveIdentityAndEquipmentAndUseEveryAsymmetricField)
{
    Client::Scenes::OwnCharacterRecord own;
    own.name[0] = std::byte{'A'};
    own.characterClass = 12;
    own.skin = 2;
    own.level = 0x1234;
    own.equipment[7].id = 0x1a04;
    own.byte3c = 0x78;
    own.word3e = 0x1234;
    own.word40 = 0x5678;
    own.word42 = 0x9abc;
    own.tailWord48 = 0x2468;
    own.tailWord4a = 0x1357;
    const Client::Protocol::Character::CharacterSelectResult reply{.experience = 0x12345678,
                                                                   .nextExperience = 0x89abcdef,
                                                                   .levelUpPoints = 0x1234,
                                                                   .strength = 0x2345,
                                                                   .agility = 0x3456,
                                                                   .vitality = 0x4567,
                                                                   .energy = 0x5678,
                                                                   .life = 0x6789,
                                                                   .maxLife = 0x789a,
                                                                   .mana = 0x89ab,
                                                                   .maxMana = 0x9abc,
                                                                   .bp = 0xabcd,
                                                                   .maxBp = 0xbcde,
                                                                   .zen = 0xcdef0123,
                                                                   .pk = 0x34,
                                                                   .controlCode = 0x45,
                                                                   .addedPoints = 0xcdef,
                                                                   .maxAddedPoints = 0xdef0,
                                                                   .leadership = 0xef01};
    own.ApplyStats(reply);
    EXPECT_EQ(own.experience, reply.experience);
    EXPECT_EQ(own.nextExperience, reply.nextExperience);
    EXPECT_EQ(own.levelUpPoints, reply.levelUpPoints);
    EXPECT_EQ(own.strength, reply.strength);
    EXPECT_EQ(own.agility, reply.agility);
    EXPECT_EQ(own.vitality, reply.vitality);
    EXPECT_EQ(own.energy, reply.energy);
    EXPECT_EQ(own.life, reply.life);
    EXPECT_EQ(own.maxLife, reply.maxLife);
    EXPECT_EQ(own.mana, reply.mana);
    EXPECT_EQ(own.maxMana, reply.maxMana);
    EXPECT_EQ(own.bp, reply.bp);
    EXPECT_EQ(own.maxBp, reply.maxBp);
    EXPECT_EQ(own.zen, reply.zen);
    EXPECT_EQ(own.addedPoints, reply.addedPoints);
    EXPECT_EQ(own.maxAddedPoints, reply.maxAddedPoints);
    EXPECT_EQ(own.leadership, reply.leadership);
    EXPECT_EQ(own.byte3c, 0);
    EXPECT_EQ(own.word3e, 0);
    EXPECT_EQ(own.word40, 0);
    EXPECT_EQ(own.word42, 0);
    EXPECT_EQ(own.tailWord48, 0);
    EXPECT_EQ(own.tailWord4a, 0);
    EXPECT_EQ(own.name[0], std::byte{'A'});
    EXPECT_EQ(own.characterClass, 12);
    EXPECT_EQ(own.skin, 2);
    EXPECT_EQ(own.level, 0x1234);
    EXPECT_EQ(own.equipment[7].id, 0x1a04);
}
} // namespace
