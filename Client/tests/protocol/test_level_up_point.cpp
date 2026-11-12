#include "Protocol/Character/LevelUpPoint.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Protocol/Outgoing.h"

#include "support/Bytes.h"
#include "support/NetworkPrinters.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstdint>

// Client::Protocol::Character, el pedido de poner un punto de los de subir de nivel (F3 06), C->S.
namespace
{
using Client::Network::Delivery;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::Outgoing;
using Client::Protocol::Character::AgilityPoint;
using Client::Protocol::Character::CommandPoint;
using Client::Protocol::Character::EnergyPoint;
using Client::Protocol::Character::StrengthPoint;
using Client::Protocol::Character::VitalityPoint;
using Client::Protocol::Character::WriteLevelUpPoint;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;

// 0x5e4695..0x5e48d3: C1 05 F3 06 0, que con el XOR32 desde el byte 3 llega al cable como C1 05 F3 7C C0, en claro
// (send() sin la cola, 0x5e494b).
TEST(LevelUpPointTest, WritesTheBytesOfTheWire)
{
    const Outgoing point = WriteLevelUpPoint(StrengthPoint);

    EXPECT_EQ(point.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(point.packet.Bytes(), Bytes({0xC1, 0x05, 0xF3, 0x7C, 0xC0})));
}

// El último byte es el botón, de arriba hacia abajo.
TEST(LevelUpPointTest, TheLastByteIsTheButton)
{
    for (const std::uint8_t stat : {StrengthPoint, AgilityPoint, VitalityPoint, EnergyPoint, CommandPoint})
    {
        const Outgoing point = WriteLevelUpPoint(stat);
        EXPECT_TRUE(SameBytes(Xor32Decode(point.packet).Bytes(), Bytes({0xC1, 0x05, 0xF3, 0x06, stat})))
            << static_cast<int>(stat);
    }
}
} // namespace
