#include "Protocol/FloorItems.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Protocol/Inventory.h"
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
namespace FloorItems = Client::Protocol::FloorItems;
namespace Inventory = Client::Protocol::Inventory;

// Un ítem inventado: una espada (0), nivel 3, durabilidad 0x2c, sin opciones.
constexpr Inventory::ItemData InventedSword{std::byte{0x00}, std::byte{0x18}, std::byte{0x2c}, std::byte{0x00},
                                            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
// El zen de 0x608350: b1 · 2^16 + b2 · 2^8 + b4 con el tipo 0x1c0f (b0 0x0f, b5 0xe0).
constexpr Inventory::ItemData InventedZen{std::byte{0x0f}, std::byte{0x00}, std::byte{0x01}, std::byte{0x80},
                                          std::byte{0xf4}, std::byte{0xe0}, std::byte{0x00}};

// 0x4d0ba0: C2, la cuenta en pkt[4] y entradas de 11 bytes desde pkt[5].
TEST(FloorItemsProtocolTest, ReadsTheFloorItemsOfThe20)
{
    const auto created = FloorItems::ReadFloorItemsCreate(
        PacketOf(Bytes({0xc2, 0x00, 0x1b, 0x20, 2,   0x80, 0x05, 130,  120,  0x00, 0x18, 0x2c, 0,   0,
                        0,    0,    0x00, 0x2a, 131, 121,  0x0f, 0x00, 0x01, 0x80, 0xf4, 0xe0, 0x00})));
    ASSERT_TRUE(created.HasValue());
    ASSERT_EQ(created.Value().items.size(), 2u);
    EXPECT_EQ(created.Value().items[0].key, 0x8005);
    EXPECT_EQ(created.Value().items[0].x, 130);
    EXPECT_EQ(created.Value().items[0].y, 120);
    EXPECT_EQ(created.Value().items[0].item, InventedSword);
    EXPECT_EQ(created.Value().items[1].key, 0x2a);
    EXPECT_EQ(created.Value().items[1].item, InventedZen);
    // Una entrada de 9 bytes (el ítem de 5 del GameServer 0.97) no alcanza.
    EXPECT_EQ(ErrorOf(FloorItems::ReadFloorItemsCreate(
                  PacketOf(Bytes({0xc2, 0x00, 0x0e, 0x20, 1, 0x00, 0x05, 130, 120, 0x00, 0x18, 0x2c, 0, 0})))),
              ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(FloorItems::ReadFloorItemsCreate(PacketOf(Bytes({0xc1, 0x05, 0x20, 0, 0})))),
              ProtocolError::WrongPacket);
}

// 0x4e3849: C2, la cuenta y claves de 2 bytes.
TEST(FloorItemsProtocolTest, ReadsTheKeysOfThe21)
{
    const auto deleted =
        FloorItems::ReadFloorItemsDelete(PacketOf(Bytes({0xc2, 0x00, 0x09, 0x21, 2, 0x00, 0x05, 0x03, 0xe8})));
    ASSERT_TRUE(deleted.HasValue());
    ASSERT_EQ(deleted.Value().keys.size(), 2u);
    EXPECT_EQ(deleted.Value().keys[0], 5);
    EXPECT_EQ(deleted.Value().keys[1], 1000);
    EXPECT_EQ(ErrorOf(FloorItems::ReadFloorItemsDelete(PacketOf(Bytes({0xc2, 0x00, 0x07, 0x21, 2, 0x00, 0x05})))),
              ProtocolError::TooShort);
}

// 0x4d0cc0: 0xff sin más; 0xfe con el zen en pkt[4..7], el alto primero; un lugar con el ítem.
TEST(FloorItemsProtocolTest, ReadsThePickUpResult)
{
    const auto nothing = FloorItems::ReadPickUpResult(PacketOf(Bytes({0xc1, 0x04, 0x22, 0xff})));
    ASSERT_TRUE(nothing.HasValue());
    EXPECT_EQ(nothing.Value().result, FloorItems::NothingPickedUp);
    const auto zen =
        FloorItems::ReadPickUpResult(PacketOf(Bytes({0xc1, 0x0b, 0x22, 0xfe, 0x00, 0x01, 0x86, 0xa0, 0, 0, 0})));
    ASSERT_TRUE(zen.HasValue());
    EXPECT_EQ(zen.Value().zen, 100000u);
    const auto item =
        FloorItems::ReadPickUpResult(PacketOf(Bytes({0xc1, 0x0b, 0x22, 0x0c, 0x00, 0x18, 0x2c, 0, 0, 0, 0})));
    ASSERT_TRUE(item.HasValue());
    EXPECT_EQ(item.Value().result, 0x0c);
    EXPECT_EQ(item.Value().item, InventedSword);
    // El C3 08 22 del GameServer 0.97 (Data[4]) no alcanza: el cliente lee siete bytes (07 Q-C1240).
    EXPECT_EQ(ErrorOf(FloorItems::ReadPickUpResult(PacketOf(Bytes({0xc1, 0x08, 0x22, 0x0c, 0x00, 0x18, 0x2c, 0})))),
              ProtocolError::TooShort);
}

TEST(FloorItemsProtocolTest, ReadsTheDropResultAndTheStatFruit)
{
    const auto drop = FloorItems::ReadDropResult(PacketOf(Bytes({0xc1, 0x05, 0x23, 1, 0x14})));
    ASSERT_TRUE(drop.HasValue());
    EXPECT_EQ(drop.Value().result, 1);
    EXPECT_EQ(drop.Value().slot, 0x14);
    const auto fruit = FloorItems::ReadStatFruit(PacketOf(Bytes({0xc1, 0x06, 0x2c, 0, 3, 1})));
    ASSERT_TRUE(fruit.HasValue());
    EXPECT_EQ(fruit.Value().result, 0);
    EXPECT_EQ(fruit.Value().points, 3);
    EXPECT_EQ(fruit.Value().stat, 1);
    // El C1 04 2C del GameServer 0.97 no trae los puntos ni el stat.
    EXPECT_EQ(ErrorOf(FloorItems::ReadStatFruit(PacketOf(Bytes({0xc1, 0x04, 0x2c, 0x23})))), ProtocolError::TooShort);
}

// 0x566755..0x566b40: C1 05 22 con el índice, el XOR32 desde pkt[3] y en C3.
TEST(FloorItemsProtocolTest, ThePickUpIsAnEncryptedC1OfFiveBytes)
{
    const auto pickUp = FloorItems::WritePickUp(0x3e7);
    EXPECT_EQ(pickUp.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Xor32Decode(pickUp.packet).Bytes(), Bytes({0xc1, 0x05, 0x22, 0x03, 0xe7})));
}

TEST(FloorItemsProtocolTest, TheDropAndTheUseAreEncryptedC1s)
{
    const auto drop = FloorItems::WriteDrop(FloorItems::Drop{.x = 130, .y = 121, .slot = 0x0d});
    EXPECT_EQ(drop.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Xor32Decode(drop.packet).Bytes(), Bytes({0xc1, 0x06, 0x23, 130, 121, 0x0d})));
    // 0x5a2c01..0x5a2e8e: el lugar + 12, el 0 y [0x78abb26].
    const auto use = FloorItems::WriteUseItem(FloorItems::UseItem{.slot = 0x0e, .target = 0, .kind = 1});
    EXPECT_EQ(use.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Xor32Decode(use.packet).Bytes(), Bytes({0xc1, 0x06, 0x26, 0x0e, 0x00, 0x01})));
}
} // namespace
