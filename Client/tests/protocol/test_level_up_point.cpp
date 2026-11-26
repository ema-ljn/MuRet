#include "Network/Xor32Packet.h"
#include "Protocol/Player/LevelUpPoint.h"

#include "Network/Delivery.h"
#include "Protocol/Outgoing.h"
#include "Network/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/MinimumSize.h"
#include "support/NetworkPrinters.h"
#include "support/Packets.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstdint>

// Client::Protocol::Player, el pedido de poner un punto de los de subir de nivel (F3 06), C->S.
namespace
{
using Client::Network::Delivery;
using Client::Network::ProtocolError;
using Client::Network::Xor32Decode;
using Client::Protocol::Outgoing;
using Client::Protocol::Player::AgilityPoint;
using Client::Protocol::Player::CommandPoint;
using Client::Protocol::Player::EnergyPoint;
using Client::Protocol::Player::ReadLevelUpPointResult;
using Client::Protocol::Player::StrengthPoint;
using Client::Protocol::Player::VitalityPoint;
using Client::Protocol::Player::WriteLevelUpPoint;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;
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
// La respuesta (0x4d3200; CGLevelUpPointAdd, 0x417b50, del GameServer 0.97 reconstruido): PMSG_LVPOINTADDRESULT, de 10
// bytes. pkt[4] es 0x10 + la estadística si se sumó (0 si no), pkt[5] el relleno, pkt[6..7] la vida o el maná máximos
// y pkt[8..9] el BP máximo, words en little endian (0x4d332d, 0x4d333b y 0x4d334d).
TEST(LevelUpPointTest, ReadsTheReplyOfTheServer)
{
    const auto vitality =
        ReadLevelUpPointResult(PacketOf(Bytes({0xc1, 0x0a, 0xf3, 0x06, 0x12, 0x00, 0x34, 0x12, 0x78, 0x56})));
    ASSERT_TRUE(vitality.HasValue());
    EXPECT_EQ(vitality.Value().result, 0x12);
    EXPECT_EQ(vitality.Value().maxLifeOrMana, 0x1234);
    EXPECT_EQ(vitality.Value().maxBp, 0x5678);
    // Sin el nibble alto, 0x4d320c no lee más que pkt[4].
    const auto failed = ReadLevelUpPointResult(PacketOf(Bytes({0xc1, 0x05, 0xf3, 0x06, 0x00})));
    ASSERT_TRUE(failed.HasValue());
    EXPECT_EQ(failed.Value().result, 0);
    // Con él, lee pkt[8..9] siempre (0x4d334d).
    EXPECT_EQ(ErrorOf(ReadLevelUpPointResult(PacketOf(Bytes({0xc1, 0x09, 0xf3, 0x06, 0x10, 0x00, 0x01, 0x00, 0x02})))),
              ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(ReadLevelUpPointResult(PacketOf(Bytes({0xc1, 0x04, 0xf3, 0x06})))), ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(ReadLevelUpPointResult(PacketOf(Bytes({0xc1, 0x05, 0xf3, 0x05, 0x10})))),
              ProtocolError::WrongPacket);
}

// 0x4d3200 reads pkt[4] and, with a point added, up to the BP at pkt[8..9].
TEST(LevelUpPointTest, ReadsItsMinimumSizeAndNotOneByteLess)
{
    Client::Tests::ExpectMinimumSize(ReadLevelUpPointResult, Bytes({0xc1, 0x05, 0xf3, 0x06, 0x00}));
    Client::Tests::ExpectMinimumSize(ReadLevelUpPointResult,
                                     Bytes({0xc1, 0x0a, 0xf3, 0x06, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00}));
}
} // namespace
