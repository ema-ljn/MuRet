#include "Protocol/Viewport.h"

#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/Packets.h"
#include "support/ViewportPackets.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{
using Client::Protocol::ProtocolError;
using Client::Protocol::Viewport::ReadEquipmentShown;
using Client::Protocol::Viewport::ReadLeave;
using Client::Protocol::Viewport::ReadMonsters;
using Client::Protocol::Viewport::ReadObjects;
using Client::Protocol::Viewport::ReadPlayers;
using Client::Protocol::Viewport::ReadPosition;
using Client::Protocol::Viewport::ReadSummons;
using Client::Protocol::Viewport::ReadTransformed;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;

// 0x4c8870..0x4c9228: los campos de cada registro del 0x12, con el bit 15 aparte.
TEST(ViewportTest, PlayersReadEachRecordAtItsAsmOffsets)
{
    const std::array<Client::Tests::ViewportPlayerRecord, 2> records{{
        {.key = 0x1234,
         .isNew = true,
         .x = 130,
         .y = 125,
         .classAndState = 0x21,
         .effects = 0x01020304,
         .name = "Hornero",
         .targetX = 131,
         .targetY = 126,
         .directionAndPk = 0x53},
        {.key = 0x7fff, .x = 1, .y = 2, .classAndState = 0x44, .name = "Carpincho1", .directionAndPk = 0x86},
    }};
    const auto packet = Client::Tests::ViewportPlayersPacket(records);
    const auto read = ReadPlayers(packet);
    ASSERT_TRUE(read.HasValue());
    ASSERT_EQ(read.Value().records.size(), 2u);
    const auto& first = read.Value().records[0];
    EXPECT_EQ(first.key, 0x1234);
    EXPECT_TRUE(first.isNew);
    EXPECT_EQ(first.x, 130);
    EXPECT_EQ(first.y, 125);
    EXPECT_EQ(first.classAndState, 0x21);
    EXPECT_EQ(std::to_integer<std::uint8_t>(first.equipment[0]), 0xff);
    EXPECT_EQ(std::to_integer<std::uint8_t>(first.equipment[8]), 0xf8);
    EXPECT_EQ(first.effects, 0x01020304u);
    EXPECT_EQ(std::to_integer<char>(first.name[0]), 'H');
    EXPECT_EQ(std::to_integer<char>(first.name[6]), 'o');
    EXPECT_EQ(std::to_integer<char>(first.name[7]), '\0');
    EXPECT_EQ(first.targetX, 131);
    EXPECT_EQ(first.targetY, 126);
    EXPECT_EQ(first.directionAndPk, 0x53);
    const auto& second = read.Value().records[1];
    EXPECT_EQ(second.key, 0x7fff);
    EXPECT_FALSE(second.isNew);
    EXPECT_EQ(std::to_integer<char>(second.name[9]), '1');
    // El paquete entero, para la cola de 0x77f3ba0.
    EXPECT_EQ(read.Value().packet.size(), 5u + 2u * 0x2c);
}

// El último registro necesita hasta su +0x28 (0x4c8f21): uno más corto no se lee.
TEST(ViewportTest, PlayersNeedTheLastByteTheHandlerReads)
{
    std::vector<std::byte> bytes(5 + 0x29);
    bytes[0] = std::byte{0xc2};
    bytes[2] = std::byte{static_cast<std::uint8_t>(bytes.size())};
    bytes[3] = std::byte{0x12};
    bytes[4] = std::byte{1};
    EXPECT_TRUE(ReadPlayers(PacketOf(bytes)).HasValue());
    bytes.pop_back();
    bytes[2] = std::byte{static_cast<std::uint8_t>(bytes.size())};
    EXPECT_EQ(ErrorOf(ReadPlayers(PacketOf(bytes))), ProtocolError::TooShort);
    // Un C1 0x12 no es el de 0x4c8860.
    EXPECT_EQ(ErrorOf(ReadPlayers(PacketOf(Bytes({0xc1, 5, 0x12, 0, 0})))), ProtocolError::WrongPacket);
}

TEST(ViewportTest, AnEmptyPlayersPacketHasNoRecords)
{
    const auto read = ReadPlayers(PacketOf(Bytes({0xc2, 0, 5, 0x12, 0})));
    ASSERT_TRUE(read.HasValue());
    EXPECT_TRUE(read.Value().records.empty());
}

// 0x4ca7a2: las claves quedan enteras, con el bit 15.
TEST(ViewportTest, LeaveKeepsTheWholeKeys)
{
    const std::array<std::uint16_t, 3> keys{0x013d, 0x8123, 0x0007};
    const auto read = ReadLeave(Client::Tests::LeavePacket(keys));
    ASSERT_TRUE(read.HasValue());
    EXPECT_EQ(read.Value().keys, (std::vector<std::uint16_t>{0x013d, 0x8123, 0x0007}));
    EXPECT_EQ(ErrorOf(ReadLeave(PacketOf(Bytes({0xc1, 6, 0x14, 2, 0, 1})))), ProtocolError::TooShort);
}

TEST(ViewportTest, TransformedReadsTheTypeAndTheShortRecord)
{
    const std::array<Client::Tests::TransformedRecordBytes, 1> records{{{.key = 0x0456,
                                                                         .isNew = true,
                                                                         .x = 70,
                                                                         .y = 80,
                                                                         .type = 0x0123,
                                                                         .effects = 0x80000,
                                                                         .name = "Tatu",
                                                                         .targetX = 71,
                                                                         .targetY = 81,
                                                                         .directionAndPk = 0x31}}};
    const auto read = ReadTransformed(Client::Tests::TransformedPacket(records));
    ASSERT_TRUE(read.HasValue());
    ASSERT_EQ(read.Value().records.size(), 1u);
    const auto& record = read.Value().records[0];
    EXPECT_EQ(record.key, 0x0456);
    EXPECT_TRUE(record.isNew);
    EXPECT_EQ(record.x, 70);
    EXPECT_EQ(record.y, 80);
    EXPECT_EQ(record.type, 0x0123);
    EXPECT_EQ(record.effects, 0x80000u);
    EXPECT_EQ(std::to_integer<char>(record.name[3]), 'u');
    EXPECT_EQ(record.targetX, 71);
    EXPECT_EQ(record.targetY, 81);
    EXPECT_EQ(record.directionAndPk, 0x31);
}

// 0x4c838e: la clave entera; el ítem hasta pkt[10] (el byte 5, el último que lee 0x54b3a0) y el 6 en 0.
TEST(ViewportTest, EquipmentShownReadsTheWholeKeyAndSixItemBytes)
{
    const auto read = ReadEquipmentShown(PacketOf(Bytes({0xc1, 11, 0x25, 0x81, 0x02, 1, 2, 3, 4, 5, 6})));
    ASSERT_TRUE(read.HasValue());
    EXPECT_EQ(read.Value().key, 0x8102);
    EXPECT_EQ(std::to_integer<std::uint8_t>(read.Value().item[0]), 1);
    EXPECT_EQ(std::to_integer<std::uint8_t>(read.Value().item[5]), 6);
    EXPECT_EQ(std::to_integer<std::uint8_t>(read.Value().item[6]), 0);
    EXPECT_EQ(ErrorOf(ReadEquipmentShown(PacketOf(Bytes({0xc1, 10, 0x25, 0, 1, 1, 2, 3, 4, 5})))),
              ProtocolError::TooShort);
}

TEST(ViewportTest, PositionReadsTheKeyAndTheCell)
{
    const auto read = ReadPosition(Client::Tests::PositionPacket(0x8001, 200, 201));
    ASSERT_TRUE(read.HasValue());
    EXPECT_EQ(read.Value().key, 0x8001);
    EXPECT_EQ(read.Value().x, 200);
    EXPECT_EQ(read.Value().y, 201);
    EXPECT_EQ(ErrorOf(ReadPosition(PacketOf(Bytes({0xc1, 6, 0xdf, 0, 1, 2})))), ProtocolError::TooShort);
}
// 0x4c9c81..0x4c9cdb: la clave con el bit 15, el tipo alto entero (con sus bits 7 y 4..6) y el tipo de (+2 & 3) y +3.
TEST(ViewportTest, MonstersReadTheTypeFromBothBytes)
{
    const std::array<Client::Tests::MonsterRecordBytes, 2> records{{{.key = 0x0123,
                                                                     .isNew = true,
                                                                     .typeHigh = 0xd1,
                                                                     .typeLow = 0x02,
                                                                     .effects = 0x20000,
                                                                     .x = 130,
                                                                     .y = 131,
                                                                     .targetX = 132,
                                                                     .targetY = 133,
                                                                     .direction = 0x30},
                                                                    {.key = 0x0124, .typeLow = 3}}};
    const auto read = ReadMonsters(Client::Tests::MonstersPacket(records));
    ASSERT_TRUE(read.HasValue());
    ASSERT_EQ(read.Value().records.size(), 2u);
    const auto& first = read.Value().records[0];
    EXPECT_EQ(first.key, 0x0123);
    EXPECT_TRUE(first.isNew);
    EXPECT_EQ(first.typeHigh, 0xd1);
    EXPECT_EQ(first.type, 0x102);
    EXPECT_EQ(first.effects, 0x20000u);
    EXPECT_EQ(first.x, 130);
    EXPECT_EQ(first.y, 131);
    EXPECT_EQ(first.targetX, 132);
    EXPECT_EQ(first.targetY, 133);
    EXPECT_EQ(first.direction, 0x30);
    EXPECT_FALSE(read.Value().records[1].isNew);
    EXPECT_EQ(read.Value().records[1].type, 3);
}

// El último registro necesita hasta su +0xc (la dirección) y el 1F hasta +0x16 (el dueño); un C1 no es el del cliente.
TEST(ViewportTest, MonsterPacketsNeedTheLastByteTheHandlerReads)
{
    std::vector<std::byte> bytes(5 + 0xd);
    bytes[0] = std::byte{0xc2};
    bytes[2] = std::byte{static_cast<std::uint8_t>(bytes.size())};
    bytes[3] = std::byte{0x13};
    bytes[4] = std::byte{1};
    EXPECT_TRUE(ReadMonsters(PacketOf(bytes)).HasValue());
    bytes.pop_back();
    bytes[2] = std::byte{static_cast<std::uint8_t>(bytes.size())};
    EXPECT_EQ(ErrorOf(ReadMonsters(PacketOf(bytes))), ProtocolError::TooShort);
    std::vector<std::byte> summon(5 + 0x17);
    summon[0] = std::byte{0xc2};
    summon[2] = std::byte{static_cast<std::uint8_t>(summon.size())};
    summon[3] = std::byte{0x1f};
    summon[4] = std::byte{1};
    EXPECT_TRUE(ReadSummons(PacketOf(summon)).HasValue());
    summon.pop_back();
    summon[2] = std::byte{static_cast<std::uint8_t>(summon.size())};
    EXPECT_EQ(ErrorOf(ReadSummons(PacketOf(summon))), ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(ReadObjects(PacketOf(Bytes({0xc1, 5, 0x68, 0, 0})))), ProtocolError::WrongPacket);
}

// 0x4ca0d7..0x4ca10c: el tipo de 16 bits, el PK abajo de la dirección y el dueño en +0xd.
TEST(ViewportTest, SummonsReadTheOwner)
{
    const std::array<Client::Tests::SummonRecordBytes, 1> records{{{.key = 0x0456,
                                                                    .isNew = true,
                                                                    .type = 0x009e,
                                                                    .effects = 1,
                                                                    .x = 10,
                                                                    .y = 11,
                                                                    .targetX = 12,
                                                                    .targetY = 13,
                                                                    .directionAndPk = 0x26,
                                                                    .owner = "Carpincho"}}};
    const auto read = ReadSummons(Client::Tests::SummonsPacket(records));
    ASSERT_TRUE(read.HasValue());
    const auto& record = read.Value().records.at(0);
    EXPECT_EQ(record.key, 0x0456);
    EXPECT_TRUE(record.isNew);
    EXPECT_EQ(record.type, 0x9e);
    EXPECT_EQ(record.effects, 1u);
    EXPECT_EQ(record.x, 10);
    EXPECT_EQ(record.y, 11);
    EXPECT_EQ(record.targetX, 12);
    EXPECT_EQ(record.targetY, 13);
    EXPECT_EQ(record.directionAndPk, 0x26);
    EXPECT_EQ(std::to_integer<char>(record.owner[0]), 'C');
    EXPECT_EQ(std::to_integer<char>(record.owner[8]), 'o');
}

// 0x4e09a4..0x4e09b9: el tipo de registro, la clase o el tipo, la clave sin el bit 15, la celda, el equipo y los
// estados.
TEST(ViewportTest, ObjectsReadTheKindAndTheEquipment)
{
    std::array<Client::Tests::ObjectRecordBytes, 1> records{
        {{.kind = 1, .classOrTypeHigh = 0x20, .typeLow = 7, .key = 0x8123, .x = 40, .y = 41, .effects = 0x10}}};
    records[0].equipment[0] = 0x12;
    const auto read = ReadObjects(Client::Tests::ObjectsPacket(records));
    ASSERT_TRUE(read.HasValue());
    const auto& record = read.Value().records.at(0);
    EXPECT_EQ(record.kind, 1);
    EXPECT_EQ(record.classOrTypeHigh, 0x20);
    EXPECT_EQ(record.typeLow, 7);
    EXPECT_EQ(record.key, 0x0123);
    EXPECT_EQ(record.x, 40);
    EXPECT_EQ(record.y, 41);
    EXPECT_EQ(std::to_integer<std::uint8_t>(record.equipment[0]), 0x12);
    EXPECT_EQ(record.effects, 0x10u);
}
} // namespace
