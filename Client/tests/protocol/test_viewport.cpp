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
using Client::Protocol::Viewport::ReadPlayers;
using Client::Protocol::Viewport::ReadPosition;
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
} // namespace
