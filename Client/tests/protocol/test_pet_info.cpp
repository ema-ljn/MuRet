#include "Protocol/Pet/PetInfo.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Protocol/Outgoing.h"

#include "support/Bytes.h"
#include "support/NetworkPrinters.h"
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
} // namespace
