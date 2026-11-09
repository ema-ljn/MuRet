#include "Scenes/OwnCharacterRecord.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Protocol/Character/CharacterSelectResult.h"
#include "World/ItemState.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace
{
using Client::Scenes::OwnCharacterRecord;

// Un tipo distinto por lugar, como el equipo viejo que deja una entrada anterior.
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
    // WinMain llena de ceros el registro (04, 0x4bafbc..0x4bafc2): el tipo arranca en 0, no en −1.
    for (const Client::World::ItemState& item : own.equipment)
    {
        EXPECT_EQ(item.id, 0);
    }
    DressWithOldTypes(own);

    own.ClearEquipmentForEntry();

    // 06, fila 7 de 0x4c3940 (0x4c47c8..0x4c47da): el tipo en −1, el dword de +4 y el byte de +0x1b
    // en 0. El loop no toca nada más del lugar.
    ASSERT_EQ(own.equipment.size(), 12u);
    for (const Client::World::ItemState& item : own.equipment)
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
    for (Client::World::ItemState& item : own.equipment)
    {
        item.occupied = 1;
    }

    own.ClearEquipmentForInventory();

    // 06, "El F3 10, el inventario" (0x4c564d..0x4c565f): el tipo en −1, el dword de +0x3c y el byte de
    // +0x1b en 0. A diferencia del de la entrada, deja el dword de +4; la durabilidad queda (Q-C203).
    for (const Client::World::ItemState& item : own.equipment)
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
    // La identidad que copia la selección (06, "La lógica"), con valores de relleno, sin nombre.
    own.name.fill(std::byte{0x7e});
    own.characterClass = 0x21;
    own.skin = 0x02;
    own.level = 0x0190;
    DressWithOldTypes(own);
    own.speedAdjustBits = 0x5a;
    own.word3e = 0x3e3e;
    own.word40 = 0x4040;
    own.word42 = 0x4242;
    // Del cable, lo que documenta 04: la celda (133, 127), el mapa 0, la dirección 0 y ff ff en
    // pkt[0x32..0x33]. Lo demás va distinto en cada campo, para que se vea un cruce: la vida y el
    // maná llegan en otro orden que el de sus offsets en el registro.
    const Client::Protocol::Character::CharacterSelectResult reply{.x = 133,
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

    // 04, la tabla de la respuesta del F3 03.
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
    // 04: "además pone en 0 el byte de +0x3c y los WORD de +0x3e, +0x40 y +0x42".
    EXPECT_EQ(own.speedAdjustBits, 0);
    EXPECT_EQ(own.word3e, 0);
    EXPECT_EQ(own.word40, 0);
    EXPECT_EQ(own.word42, 0);
    // La identidad no la escribe, y el equipo sigue viejo para 0x512a30 (06, fila 5).
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

    // Q-C144: el original copia pkt[0x34..0x37], lo que haya quedado en el buffer después de los
    // 0x34 bytes del paquete. MuRet deja los dos WORD en 0 y lo avisa.
    EXPECT_EQ(own.word48, 0);
    EXPECT_EQ(own.word4a, 0);
    ASSERT_EQ(sink.lines.size(), 1u);
    EXPECT_EQ(sink.levels[0], Client::Core::LogLevel::Warning);
    EXPECT_NE(sink.lines[0].find("Q-C144"), std::string::npos);
}
} // namespace
