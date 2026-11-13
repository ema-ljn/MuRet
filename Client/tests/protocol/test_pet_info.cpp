#include "Protocol/Pet/PetInfo.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/NetworkPrinters.h"
#include "support/Packets.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

// Client::Protocol::Pet, el pedido de la información de una mascota (A9), C->S.
namespace
{
using Client::Network::Delivery;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::Outgoing;
using Client::Protocol::Pet::DarkHorsePet;
using Client::Protocol::Pet::InventoryPlace;
using Client::Protocol::Pet::LoginScenePetInfoRequest;
using Client::Protocol::Pet::PetSlot;
using Client::Protocol::Pet::WritePetInfoRequest;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;

// El del ConnectServer (04 § El flujo de conexión): C1 06 A9 00 00 01, que con el XOR32 llega al
// cable como C1 06 A9 20 9C 2F, en claro.
TEST(PetInfoTest, WritesTheBytesOfTheWire)
{
    const Outgoing request = WritePetInfoRequest(LoginScenePetInfoRequest);

    EXPECT_EQ(request.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(request.packet.Bytes(), Bytes({0xC1, 0x06, 0xA9, 0x20, 0x9C, 0x2F})));
    EXPECT_TRUE(SameBytes(Xor32Decode(request.packet).Bytes(), Bytes({0xC1, 0x06, 0xA9, 0x00, 0x00, 0x01})));
}

// La mascota, el lugar y la casilla, en ese orden: con tres valores distintos, uno cambiado de
// lugar se ve. El otro es el del GameServer (0x424230), el caballo oscuro en la casilla de la
// mascota, que sale en la CL8.
TEST(PetInfoTest, PutsEachFieldAtItsOffset)
{
    const Outgoing distinct = WritePetInfoRequest({0x11, 0x22, 0x33});
    const Outgoing horse = WritePetInfoRequest({DarkHorsePet, InventoryPlace, PetSlot});

    EXPECT_TRUE(SameBytes(Xor32Decode(distinct.packet).Bytes(), Bytes({0xC1, 0x06, 0xA9, 0x11, 0x22, 0x33})));
    EXPECT_TRUE(SameBytes(Xor32Decode(horse.packet).Bytes(), Bytes({0xC1, 0x06, 0xA9, 0x01, 0x00, 0x08})));
}

// 0x4e4ca1..0x4e4cb8: la respuesta, con la mascota, la clase, la casilla y el nivel de pkt[3] a pkt[6] y la experiencia
// en el DWORD de pkt[8], little endian; pkt[7] no lo lee nadie (CL9 hito 4).
TEST(PetInfoTest, TheReplyReadsEachFieldAndTheExperienceInLittleEndian)
{
    const auto reply = Client::Protocol::Pet::ReadPetInfoReply(
        Client::Tests::PacketOf(Bytes({0xc1, 12, 0xa9, 1, 0xfe, 8, 42, 0xee, 0x78, 0x56, 0x34, 0x12})));
    ASSERT_TRUE(reply.HasValue());
    EXPECT_EQ(reply.Value().pet, 1);
    EXPECT_EQ(reply.Value().kind, 0xfe);
    EXPECT_EQ(reply.Value().slot, 8);
    EXPECT_EQ(reply.Value().level, 42);
    EXPECT_EQ(reply.Value().experience, 0x12345678u);
    using Client::Protocol::ProtocolError;
    using Client::Tests::ErrorOf;
    EXPECT_EQ(ErrorOf(Client::Protocol::Pet::ReadPetInfoReply(
                  Client::Tests::PacketOf(Bytes({0xc1, 11, 0xa9, 1, 0xfe, 8, 42, 0xee, 0x78, 0x56, 0x34})))),
              ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(Client::Protocol::Pet::ReadPetInfoReply(
                  Client::Tests::PacketOf(Bytes({0xc1, 12, 0xa8, 1, 0xfe, 8, 42, 0xee, 0x78, 0x56, 0x34, 0x12})))),
              ProtocolError::WrongPacket);
}
} // namespace
