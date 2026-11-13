#include "Protocol/Combat.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Network/FoxAntiObfuscation.h"
#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/Packets.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::ProtocolError;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;
using Client::Tests::SameBytes;
namespace Combat = Client::Protocol::Combat;

// 0x5650c0 (0x565a49..0x56601e): C1 07 D7 claveH claveL 78 rumbo, XOR32; FoxAnti lo saca como A4, el PMSG_ATTACK de la
// GS (CGAttack 0x4254d0: NumberH NumberL AttackAction DirDis).
TEST(CombatProtocolTest, TheAttackIsAnXor32D7ThatFoxAntiSendsAsA4)
{
    auto sent = Combat::WriteAttack(0x1234, Combat::AttackAction, 5);
    EXPECT_EQ(sent.delivery, Client::Network::Delivery::Clear);
    EXPECT_TRUE(SameBytes(Xor32Decode(sent.packet).Bytes(), Bytes({0xc1, 0x07, 0xd7, 0x12, 0x34, 0x78, 0x05})));
    Client::Network::FoxAntiObfuscation foxAnti;
    const auto wire = foxAnti.BeforeSend(std::move(sent.packet));
    EXPECT_EQ(wire.Bytes()[2], std::byte{0xa4});
    EXPECT_EQ(wire.Bytes().size(), 7u);
}

// La clave viaja entera: +0x1f4 de un monstruo con el bit 15 no se enmascara (0x565a8c..0x565b5a).
TEST(CombatProtocolTest, TheAttackKeepsTheWholeKey)
{
    const auto sent = Combat::WriteAttack(0x8001, Combat::AttackAction, 7);
    EXPECT_TRUE(SameBytes(Xor32Decode(sent.packet).Bytes(), Bytes({0xc1, 0x07, 0xd7, 0x80, 0x01, 0x78, 0x07})));
}

// 0x4cf2aa..0x4cf4a5 y 0x4cfd12..0x4cff5c: con el flag de descifrado en 0, C1 06 F1 03 00 y el opcode, en C3.
TEST(CombatProtocolTest, AClearKillReplyIsReportedWithItsOpcode)
{
    for (const std::uint8_t opcode : {Combat::KillExperienceOpcode, Combat::ExtendedKillExperienceOpcode})
    {
        const auto report = Combat::WriteClearKillReport(opcode);
        EXPECT_EQ(report.delivery, Client::Network::Delivery::Encrypted);
        EXPECT_TRUE(SameBytes(Xor32Decode(report.packet).Bytes(), Bytes({0xc1, 0x06, 0xf1, 0x03, 0x00, opcode})));
    }
}

// PMSG_KILLPLAYER_EXP de la GS (GCKillPlayerExpSend 0x425a70, 9 bytes): NumberH NumberL ExpH ExpL DamageH DamageL;
// el bit 7 de NumberH es el MSBFlag.
TEST(CombatProtocolTest, ReadsTheKillExperienceLikeTheGameServerStruct)
{
    const auto plain = Combat::ReadKillExperience(PacketOf(Bytes({0xc1, 9, 0x16, 0x01, 0x2c, 0x03, 0xe8, 0x00, 0x2a})));
    ASSERT_TRUE(plain.HasValue());
    EXPECT_EQ(plain.Value().key, 0x012c);
    EXPECT_FALSE(plain.Value().shock);
    EXPECT_EQ(plain.Value().amount, 1000u);
    EXPECT_EQ(plain.Value().damage, 42);
    const auto shock = Combat::ReadKillExperience(PacketOf(Bytes({0xc1, 9, 0x16, 0x81, 0x2c, 0xff, 0xff, 0x12, 0x34})));
    ASSERT_TRUE(shock.HasValue());
    EXPECT_EQ(shock.Value().key, 0x012c);
    EXPECT_TRUE(shock.Value().shock);
    EXPECT_EQ(shock.Value().amount, 0xffffu);
    EXPECT_EQ(shock.Value().damage, 0x1234);
    EXPECT_EQ(ErrorOf(Combat::ReadKillExperience(PacketOf(Bytes({0xc1, 8, 0x16, 0, 1, 0, 2, 0})))),
              ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(Combat::ReadKillExperience(PacketOf(Bytes({0xc1, 9, 0x17, 0, 1, 0, 2, 0, 3})))),
              ProtocolError::WrongPacket);
}

// PMSG_KILLPLAYER_EXT (la recompensa de Blood Castle, 0x49af90, 12 bytes): ExpH en +6 tras el relleno natural de +5,
// los dos WORD en little endian. La GS manda NumberH/L en FF FF: la clave 0x7fff con el bit 15.
TEST(CombatProtocolTest, ReadsTheExtendedKillExperienceLikeTheGameServerStruct)
{
    const auto read = Combat::ReadExtendedKillExperience(
        PacketOf(Bytes({0xc1, 12, 0x9c, 0xff, 0xff, 0xcc, 0x01, 0x00, 0x20, 0x4e, 0x00, 0x07})));
    ASSERT_TRUE(read.HasValue());
    EXPECT_EQ(read.Value().key, 0x7fff);
    EXPECT_TRUE(read.Value().shock);
    EXPECT_EQ(read.Value().amount, 0x10000u + 20000u);
    EXPECT_EQ(read.Value().damage, 7);
    EXPECT_EQ(ErrorOf(Combat::ReadExtendedKillExperience(
                  PacketOf(Bytes({0xc1, 11, 0x9c, 0xff, 0xff, 0, 1, 0, 0x20, 0x4e, 0})))),
              ProtocolError::TooShort);
}

// 0x17: la GS 0.97 manda 5 bytes (PMSG_DIEPLAYER, 0x425bb0); el cliente lee hasta pkt[7]. Sin esos bytes, 0 (Q-C1161).
TEST(CombatProtocolTest, ReadsTheDeathWithOrWithoutTheKiller)
{
    const auto full = Combat::ReadDeath(PacketOf(Bytes({0xc1, 8, 0x17, 0x80, 0x05, 0x28, 0x00, 0x09})));
    ASSERT_TRUE(full.HasValue());
    EXPECT_EQ(full.Value().key, 0x8005);
    EXPECT_EQ(full.Value().skill, 0x28);
    EXPECT_EQ(full.Value().killer, 9);
    const auto server = Combat::ReadDeath(PacketOf(Bytes({0xc1, 5, 0x17, 0x00, 0x05})));
    ASSERT_TRUE(server.HasValue());
    EXPECT_EQ(server.Value().key, 5);
    EXPECT_EQ(server.Value().skill, 0);
    EXPECT_EQ(server.Value().killer, 0);
    EXPECT_EQ(ErrorOf(Combat::ReadDeath(PacketOf(Bytes({0xc1, 4, 0x17, 0x00})))), ProtocolError::TooShort);
}
} // namespace
