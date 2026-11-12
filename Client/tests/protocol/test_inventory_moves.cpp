#include "Protocol/Inventory.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/Packets.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstddef>

namespace
{
using Client::Network::Delivery;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::ProtocolError;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;
using Client::Tests::SameBytes;
namespace Inventory = Client::Protocol::Inventory;

// Un ítem inventado: el byte 0, el nivel y las opciones, la durabilidad, los dos de opciones, el grupo y el último.
constexpr Inventory::ItemData InventedItem{std::byte{0x21}, std::byte{0x0b}, std::byte{0x2c}, std::byte{0x00},
                                           std::byte{0x00}, std::byte{0x70}, std::byte{0x00}};

// 0x449d80: C1 0E 24, el origen, los siete bytes y el destino, con el XOR32 desde pkt[3] y para cifrar como C3.
TEST(InventoryProtocolTest, TheItemMoveIsAnEncryptedC1OfFourteenBytes)
{
    const auto move = Inventory::WriteItemMove(Inventory::ItemMove{
        .sourceFlag = 0, .sourceSlot = 0x15, .item = InventedItem, .targetFlag = 0, .targetSlot = 3});
    EXPECT_EQ(move.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Xor32Decode(move.packet).Bytes(),
                          Bytes({0xc1, 0x0e, 0x24, 0, 0x15, 0x21, 0x0b, 0x2c, 0, 0, 0x70, 0, 0, 3})));
}

// 0x4d1400: el C3 0C 24 del servidor, ya descifrado.
TEST(InventoryProtocolTest, ReadsTheItemMoveResult)
{
    const auto result =
        Inventory::ReadItemMoveResult(PacketOf(Bytes({0xc1, 0x0c, 0x24, 0, 3, 0x21, 0x0b, 0x2c, 0, 0, 0x70, 0})));
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value().result, 0);
    EXPECT_EQ(result.Value().slot, 3);
    EXPECT_EQ(result.Value().item, InventedItem);
    EXPECT_EQ(ErrorOf(Inventory::ReadItemMoveResult(PacketOf(Bytes({0xc1, 0x05, 0x24, 0xff, 0})))),
              ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(Inventory::ReadItemMoveResult(PacketOf(Bytes({0xc1, 0x05, 0x28, 0xff, 0})))),
              ProtocolError::WrongPacket);
}

TEST(InventoryProtocolTest, ReadsTheDeleteAndTheDurability)
{
    const auto deleted = Inventory::ReadItemDelete(PacketOf(Bytes({0xc1, 0x05, 0x28, 0x14, 1})));
    ASSERT_TRUE(deleted.HasValue());
    EXPECT_EQ(deleted.Value().slot, 0x14);
    EXPECT_EQ(deleted.Value().endsTheWait, 1);
    const auto durability = Inventory::ReadDurability(PacketOf(Bytes({0xc1, 0x06, 0x2a, 9, 0x31, 0})));
    ASSERT_TRUE(durability.HasValue());
    EXPECT_EQ(durability.Value().slot, 9);
    EXPECT_EQ(durability.Value().durability, 0x31);
    EXPECT_EQ(durability.Value().endsTheWait, 0);
    EXPECT_EQ(ErrorOf(Inventory::ReadDurability(PacketOf(Bytes({0xc1, 0x05, 0x2a, 9, 0x31})))),
              ProtocolError::TooShort);
}

// 0x4e316e: la clave en pkt[4..5] (alto y bajo), pkt[6] sin leer y el equipo de 17 bytes desde pkt[7].
TEST(InventoryProtocolTest, ReadsTheEquipmentChangeFromTheSeventhByte)
{
    auto bytes = Bytes({0xc1, 0x18, 0xf3, 0x13, 0x12, 0x34, 0x77});
    for (int i = 0; i < 17; ++i)
        bytes.push_back(static_cast<std::byte>(i + 1));
    const auto change = Inventory::ReadEquipmentChange(PacketOf(bytes));
    ASSERT_TRUE(change.HasValue());
    EXPECT_EQ(change.Value().key, 0x1234);
    EXPECT_EQ(change.Value().equipment[0], std::byte{1});
    EXPECT_EQ(change.Value().equipment[16], std::byte{17});
}

// 0x4e31a3: el lugar en pkt[4] y el ítem desde pkt[5].
TEST(InventoryProtocolTest, ReadsTheItemPut)
{
    const auto put =
        Inventory::ReadItemPut(PacketOf(Bytes({0xc1, 0x0c, 0xf3, 0x14, 0x20, 0x21, 0x0b, 0x2c, 0, 0, 0x70, 0})));
    ASSERT_TRUE(put.HasValue());
    EXPECT_EQ(put.Value().slot, 0x20);
    EXPECT_EQ(put.Value().item, InventedItem);
}
} // namespace
