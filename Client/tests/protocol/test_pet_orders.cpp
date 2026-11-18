#include "Protocol/Pet/PetOrders.h"

#include "Network/Delivery.h"
#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/Packets.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

// Client::Protocol::Pet, las órdenes del cuervo que llegan (A7 y A8, CL9 hito 4) y el A7 que sale, con claves
// inventadas.
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

// 0x416b05..0x417095: C1 07 A7 00 78 FF FF con el XOR32 de 0x416bce (la clave de 32 bytes desde e7 6d 3a 89, cada
// byte desde pkt[3] con el anterior ya cifrado), en claro por send().
TEST(PetOrdersTest, TheStopCommandIsTheXoredA7OfTheStep)
{
    const auto command =
        Pet::WritePetCommand({.pet = Pet::RavenPet, .command = Pet::StopTheAttackCommand, .key = Pet::NoTargetKey});
    EXPECT_EQ(command.delivery, Client::Network::Delivery::Clear);
    EXPECT_TRUE(Client::Tests::SameBytes(command.packet.Bytes(), Bytes({0xc1, 7, 0xa7, 0x2e, 0xea, 0xa7, 0xc7})));
}
} // namespace
