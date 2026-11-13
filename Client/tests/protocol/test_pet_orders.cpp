#include "Protocol/Pet/PetOrders.h"

#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/Packets.h"

#include <gtest/gtest.h>

// Client::Protocol::Pet, las órdenes del cuervo que llegan (A7 y A8, CL9 hito 4), con claves inventadas.
namespace
{
using Client::Protocol::ProtocolError;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;
namespace Pet = Client::Protocol::Pet;

// 0x4e4c0a: la orden en pkt[4] y la clave en pkt[5..6], sin mirar pkt[3].
TEST(PetOrdersTest, TheOrderReadsAfterTheUnreadByte)
{
    const auto order = Pet::ReadPetOrder(PacketOf(Bytes({0xc1, 7, 0xa7, 0xee, 3, 0x12, 0x34})));
    ASSERT_TRUE(order.HasValue());
    EXPECT_EQ(order.Value().order, 3);
    EXPECT_EQ(order.Value().key, 0x1234);
    EXPECT_EQ(ErrorOf(Pet::ReadPetOrder(PacketOf(Bytes({0xc1, 6, 0xa7, 0, 3, 0x12})))), ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(Pet::ReadPetOrder(PacketOf(Bytes({0xc1, 7, 0xa8, 0, 3, 0x12, 0x34})))),
              ProtocolError::WrongPacket);
}

// 0x4e4c42: el tipo en pkt[4], el dueño en pkt[5..6] y el blanco en pkt[7..8].
TEST(PetOrdersTest, TheAttackReadsTheOwnerAndTheTarget)
{
    const auto attack = Pet::ReadPetAttack(PacketOf(Bytes({0xc1, 9, 0xa8, 0xee, 1, 0x12, 0x34, 0x05, 0x67})));
    ASSERT_TRUE(attack.HasValue());
    EXPECT_EQ(attack.Value().kind, 1);
    EXPECT_EQ(attack.Value().owner, 0x1234);
    EXPECT_EQ(attack.Value().target, 0x0567);
    EXPECT_EQ(ErrorOf(Pet::ReadPetAttack(PacketOf(Bytes({0xc1, 8, 0xa8, 0, 1, 0x12, 0x34, 0x05})))),
              ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(Pet::ReadPetAttack(PacketOf(Bytes({0xc1, 3, 0xa8})))), ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(Pet::ReadPetAttack(PacketOf(Bytes({0xc2, 0, 10, 0xa8, 0, 1, 0x12, 0x34, 0x05, 0x67})))),
              ProtocolError::WrongPacket);
}
} // namespace
