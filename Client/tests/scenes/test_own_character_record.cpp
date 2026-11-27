#include "Kernel/OwnCharacterRecord.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Protocol/FrontEnd/CharacterSelectResult.h"
#include "Kernel/ItemState.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace
{
using Client::Kernel::OwnCharacterRecord;

// A different type per place, like the old equipment a previous entry leaves.
std::uint16_t OldTypeOf(std::size_t slot)
{
    return static_cast<std::uint16_t>(0x0100 + slot);
}

void DressWithOldTypes(OwnCharacterRecord& own)
{
    for (std::size_t slot = 0; slot < own.equipment.size(); ++slot)
    {
        own.equipment[slot].id = OldTypeOf(slot);
        own.equipment[slot].scalar1Low = 0x78;
        own.equipment[slot].flags1b = 0x5a;
        own.equipment[slot].flags1c = 0x3c;
        own.equipment[slot].durability = 0x2d;
    }
}

TEST(OwnCharacterRecordTest, TheEntryClearEmptiesTypeLevelAndFlags1bOfTheTwelveSlots)
{
    OwnCharacterRecord own;
    // WinMain zeroes the record (04, 0x4bafbc..0x4bafc2): the type starts at 0, not at −1.
    for (const Client::Kernel::ItemState& item : own.equipment)
    {
        EXPECT_EQ(item.id, 0);
    }
    DressWithOldTypes(own);

    own.ClearEquipmentForEntry();

    // 06, row 7 of 0x4c3940 (0x4c47c8..0x4c47da): the type at −1, the dword of +4 and the byte of +0x1b at 0. The
    // loop touches nothing else of the place.
    ASSERT_EQ(own.equipment.size(), 12u);
    for (const Client::Kernel::ItemState& item : own.equipment)
    {
        EXPECT_EQ(item.id, 0xffff);
        EXPECT_EQ(item.scalar1Low, 0u);
        EXPECT_EQ(item.flags1b, 0);
        EXPECT_EQ(item.flags1c, 0x3c);
        EXPECT_EQ(item.durability, 0x2d);
    }
}

TEST(OwnCharacterRecordTest, TheInventoryClearEmptiesTypeOccupiedAndFlags1bOfTheTwelveSlots)
{
    OwnCharacterRecord own;
    DressWithOldTypes(own);
    for (Client::Kernel::ItemState& item : own.equipment)
    {
        item.occupied = 1;
    }

    own.ClearEquipmentForInventory();

    // 06, "El F3 10, el inventario" (0x4c564d..0x4c565f): the type at −1, the dword of +0x3c and the byte of +0x1b at
    // 0. Unlike the entry's, it keeps the dword of +4; the durability stays (Q-C203).
    for (const Client::Kernel::ItemState& item : own.equipment)
    {
        EXPECT_EQ(item.id, 0xffff);
        EXPECT_EQ(item.occupied, 0u);
        EXPECT_EQ(item.flags1b, 0);
        EXPECT_EQ(item.scalar1Low, 0x78u);
        EXPECT_EQ(item.flags1c, 0x3c);
        EXPECT_EQ(item.durability, 0x2d);
    }
}

TEST(OwnCharacterRecordTest, ReceivedStatsGoToTheirFieldsAndKeepIdentityAndEquipment)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    OwnCharacterRecord own;
    // The identity the selection copies (06, "La lógica"), with filler values, no name.
    own.name.fill(std::byte{0x7e});
    own.characterClass = 0x21;
    own.skin = 0x02;
    own.level = 0x0190;
    DressWithOldTypes(own);
    own.speedAdjustBits = 0x5a;
    own.word3e = 0x3e3e;
    own.word40 = 0x4040;
    own.word42 = 0x4242;
    // From the wire, what 04 documents: cell (133, 127), map 0, direction 0 and ff ff in pkt[0x32..0x33]. The rest
    // differs in each field, so that a crossing shows: the life and the mana arrive in another order than their
    // offsets in the record.
    const Client::Protocol::FrontEnd::CharacterSelectResult reply{.x = 133,
                                                                  .y = 127,
                                                                  .map = 0,
                                                                  .direction = 0,
                                                                  .experience = 0x01020304,
                                                                  .nextExperience = 0x05060708,
                                                                  .levelUpPoints = 0x0a0b,
                                                                  .strength = 0x0c0d,
                                                                  .agility = 0x0e0f,
                                                                  .vitality = 0x1011,
                                                                  .energy = 0x1213,
                                                                  .life = 0x1415,
                                                                  .maxLife = 0x1617,
                                                                  .mana = 0x1819,
                                                                  .maxMana = 0x1a1b,
                                                                  .bp = 0x1c1d,
                                                                  .maxBp = 0x1e1f,
                                                                  .zen = 0x20212223,
                                                                  .pk = 0x24,
                                                                  .controlCode = 0x25,
                                                                  .addedPoints = 0x2627,
                                                                  .maxAddedPoints = 0x2829,
                                                                  .leadership = 0xffff};

    own.ApplyStats(reply, log);

    // 04, the F3 03 reply's table.
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
    EXPECT_EQ(own.leadership, 0xffff);
    // 04: it also zeroes the byte of +0x3c and the WORDs of +0x3e, +0x40 and +0x42.
    EXPECT_EQ(own.speedAdjustBits, 0);
    EXPECT_EQ(own.word3e, 0);
    EXPECT_EQ(own.word40, 0);
    EXPECT_EQ(own.word42, 0);
    // It does not write the identity, and the equipment stays old for 0x512a30 (06, row 5).
    for (const std::byte nameByte : own.name)
    {
        EXPECT_EQ(nameByte, std::byte{0x7e});
    }
    EXPECT_EQ(own.characterClass, 0x21);
    EXPECT_EQ(own.skin, 0x02);
    EXPECT_EQ(own.level, 0x0190);
    for (std::size_t slot = 0; slot < own.equipment.size(); ++slot)
    {
        EXPECT_EQ(own.equipment[slot].id, OldTypeOf(slot));
    }
}

TEST(OwnCharacterRecordTest, TheWordsPastThePacketGoToZeroWithAWarning)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    OwnCharacterRecord own;
    own.word48 = 0x4848;
    own.word4a = 0x4a4a;

    own.ApplyStats({}, log);

    // Q-C144: the original copies pkt[0x34..0x37], whatever stayed in the buffer after the packet's 0x34 bytes. MuRet
    // leaves the two WORDs at 0 and says so.
    EXPECT_EQ(own.word48, 0);
    EXPECT_EQ(own.word4a, 0);
    ASSERT_EQ(sink.lines.size(), 1u);
    EXPECT_EQ(sink.levels[0], Client::Core::LogLevel::Warning);
    EXPECT_NE(sink.lines[0].find("Q-C144"), std::string::npos);
}
} // namespace
