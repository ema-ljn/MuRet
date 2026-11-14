#include "Protocol/CastleBattle.h"

#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/Packets.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

// Los paquetes de la batalla del castillo (CL9 hito 13; 04 § Castle Siege, la batalla): B9 01/02/03/05, BA, BB y
// B2 20. El GameServer 0.97 no tiene Castle Siege: cada byte sale del ASM del cliente (*estático*). Los nombres son
// inventados.
namespace
{
namespace Battle = Client::Protocol::CastleBattle;
using Client::Protocol::ProtocolError;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;

Battle::CastleBattlePacket Read(std::vector<std::byte> bytes)
{
    auto read = Battle::ReadCastleBattlePacket(PacketOf(std::move(bytes)));
    EXPECT_TRUE(read.HasValue());
    return read.HasValue() ? std::move(read).Value() : Battle::CastleBattlePacket{};
}

std::optional<ProtocolError> ErrorReading(std::vector<std::byte> bytes)
{
    return ErrorOf(Battle::ReadCastleBattlePacket(PacketOf(std::move(bytes))));
}

// B9 01 (0x4e0750): la clave de pkt[4] (arriba) y pkt[5], y el byte de pkt[6].
TEST(CastleBattleProtocolTest, NpcLevelReadsTheKeyHighFirstAndTheLevel)
{
    const auto level = std::get<Battle::NpcLevel>(Read(Bytes({0xc1, 7, 0xb9, 0x01, 0x12, 0x34, 0x05})));
    EXPECT_EQ(level.key, 0x1234);
    EXPECT_EQ(level.level, 5);
}

TEST(CastleBattleProtocolTest, NpcLevelWithoutPkt6IsTooShort)
{
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 6, 0xb9, 0x01, 0x12, 0x34})), ProtocolError::TooShort);
}

// En un C2 el subcódigo es pkt[4] y la clave también empieza ahí: lee a desplazamientos fijos.
TEST(CastleBattleProtocolTest, InAC2TheKeyStartsAtTheSubcode)
{
    const auto level = std::get<Battle::NpcLevel>(Read(Bytes({0xc2, 0, 7, 0xb9, 0x01, 0x80, 0x02})));
    EXPECT_EQ(level.key, 0x0180);
    EXPECT_EQ(level.level, 2);
}

// B9 02 (0x4e0790): los 32 bytes de pkt[4..0x23], tal cual; los sobrantes se ignoran.
TEST(CastleBattleProtocolTest, OwnerMarkKeepsThe32PackedBytes)
{
    std::vector<std::byte> packet = Bytes({0xc1, 0x25, 0xb9, 0x02});
    for (int i = 0; i < 32; ++i)
    {
        packet.push_back(static_cast<std::byte>((i * 0x11) + 1));
    }
    packet.push_back(std::byte{0xee});
    const auto mark = std::get<Battle::OwnerMark>(Read(std::move(packet)));
    for (std::size_t i = 0; i < Battle::PackedMarkSize; ++i)
    {
        EXPECT_EQ(mark.packed.at(i), static_cast<std::uint8_t>((i * 0x11) + 1)) << i;
    }
}

TEST(CastleBattleProtocolTest, OwnerMarkWithout32BytesIsTooShort)
{
    std::vector<std::byte> packet = Bytes({0xc1, 0x23, 0xb9, 0x02});
    packet.resize(0x23);
    EXPECT_EQ(ErrorReading(std::move(packet)), ProtocolError::TooShort);
}

// B9 03 (0x4e07f0) con pkt[4] distinto de 0: pkt[5] y los dwords de pkt+8, +0xc y +0x10, el byte de abajo primero.
TEST(CastleBattleProtocolTest, HuntZoneInfoReadsEachFieldAtItsOffset)
{
    const auto info = std::get<Battle::HuntZoneInfo>(Read(Bytes({0xc1, 0x14, 0xb9, 0x03, 0x02, 0x07, 0xaa, 0xbb, //
                                                                 0x10, 0x27, 0x00, 0x00, // 10000
                                                                 0x01, 0x02, 0x03, 0x04, //
                                                                 0xff, 0xff, 0xff, 0x7f})));
    EXPECT_EQ(info.mode, 2);
    EXPECT_EQ(info.allowed, 7);
    EXPECT_EQ(info.fee, 10000u);
    EXPECT_EQ(info.at0c, 0x04030201u);
    EXPECT_EQ(info.at10, 0x7fffffffu);
}

// Con pkt[4] en 0 no lee nada más: un B9 03 de 5 bytes alcanza.
TEST(CastleBattleProtocolTest, HuntZoneInfoWithZeroReadsOnlyPkt4)
{
    const auto info = std::get<Battle::HuntZoneInfo>(Read(Bytes({0xc1, 5, 0xb9, 0x03, 0x00})));
    EXPECT_EQ(info.mode, 0);
    EXPECT_EQ(info.allowed, 0);
    EXPECT_EQ(info.fee, 0u);
}

TEST(CastleBattleProtocolTest, HuntZoneInfoWithoutTheLastDwordIsTooShort)
{
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 0x13, 0xb9, 0x03, 0x01, 0x01, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0})),
              ProtocolError::TooShort);
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 4, 0xb9, 0x03})), ProtocolError::TooShort);
}

// B9 05 (0x4e0860): pkt[4].
TEST(CastleBattleProtocolTest, HuntZoneEntryResultReadsPkt4)
{
    EXPECT_EQ(std::get<Battle::HuntZoneEntryResult>(Read(Bytes({0xc1, 5, 0xb9, 0x05, 0x01}))).result, 1);
    EXPECT_EQ(std::get<Battle::HuntZoneEntryResult>(Read(Bytes({0xc1, 5, 0xb9, 0x05, 0x00}))).result, 0);
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 4, 0xb9, 0x05})), ProtocolError::TooShort);
}

// 0x4e58b0: el 04 y desde el 06 caen al default (0x4e58c8); el 00 también.
TEST(CastleBattleProtocolTest, OtherBattleSubcodesAreWrongPackets)
{
    for (const int subcode : {0x00, 0x04, 0x06, 0x10, 0xff})
    {
        EXPECT_EQ(ErrorReading(Bytes({0xc1, 7, 0xb9, subcode, 1, 2, 3})), ProtocolError::WrongPacket) << subcode;
    }
}

// BA (0x4cf1b0): la clave de pkt[3] y pkt[4] (también en un C2), cuál de pkt[5] y, solo con 0x28, pkt[6].
TEST(CastleBattleProtocolTest, ActorFieldReadsTheValueOnlyWithKind28)
{
    const auto with = std::get<Battle::ActorField>(Read(Bytes({0xc1, 7, 0xba, 0x02, 0x9a, 0x28, 0x03})));
    EXPECT_EQ(with.key, 0x029a);
    EXPECT_EQ(with.kind, 0x28);
    EXPECT_EQ(with.value, 3);

    const auto without = std::get<Battle::ActorField>(Read(Bytes({0xc1, 6, 0xba, 0x02, 0x9a, 0x27})));
    EXPECT_EQ(without.kind, 0x27);
    EXPECT_EQ(without.value, 0);

    EXPECT_EQ(ErrorReading(Bytes({0xc1, 6, 0xba, 0x02, 0x9a, 0x28})), ProtocolError::TooShort);
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 5, 0xba, 0x02, 0x9a})), ProtocolError::TooShort);
}

// En un C2, pkt[3] es el opcode: la clave sale con él arriba.
TEST(CastleBattleProtocolTest, ActorFieldInAC2ReadsTheSameOffsets)
{
    const auto field = std::get<Battle::ActorField>(Read(Bytes({0xc2, 0, 7, 0xba, 0x05, 0x28, 0x09})));
    EXPECT_EQ(field.key, 0xba05);
    EXPECT_EQ(field.kind, 0x28);
    EXPECT_EQ(field.value, 9);
}

// BB (0x4e0f30): la cuenta de pkt[4] sin signo y las ternas desde pkt+5.
TEST(CastleBattleProtocolTest, MinimapMembersReadsTheTriplesFromPkt5)
{
    const auto members = std::get<Battle::MinimapMembers>(
        Read(Bytes({0xc1, 0x0b, 0xbb, 0x00, 0x02, 0x00, 0x10, 0x20, 0xff, 0x30, 0x40})));
    ASSERT_EQ(members.members.size(), 2u);
    EXPECT_EQ(members.members[0].index, 0);
    EXPECT_EQ(members.members[0].x, 0x10);
    EXPECT_EQ(members.members[0].y, 0x20);
    EXPECT_EQ(members.members[1].index, 0xff);
    EXPECT_EQ(members.members[1].x, 0x30);
    EXPECT_EQ(members.members[1].y, 0x40);
}

TEST(CastleBattleProtocolTest, MinimapMembersWithZeroIsEmptyAndShortIsTooShort)
{
    EXPECT_TRUE(std::get<Battle::MinimapMembers>(Read(Bytes({0xc1, 5, 0xbb, 0x00, 0x00}))).members.empty());
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 0x0a, 0xbb, 0x00, 0x02, 0x00, 0x10, 0x20, 0x01, 0x30})),
              ProtocolError::TooShort);
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 4, 0xbb, 0x00})), ProtocolError::TooShort);
}

// B2 20 (0x4e04e0): los 0x1b bytes desde pkt+0 tal cual, con la clave de pkt[4] (arriba) y pkt[5].
std::vector<std::byte> SwitchHolderPacket(int keyHigh, int keyLow)
{
    return Bytes({0xc1, 0x1c, 0xb2, 0x20, keyHigh, keyLow, 0x01, 0x02,                         //
                  0x11, 0x22, 0x33, 0x44, 0x55,    0x66,   0x77, 0x88,                         //
                  'T',  'e',  'r',  'o',  0,       'q',    'w',  'e',  'r', 't', 0x5a, 0x99}); // basura detrás del 0
}

TEST(CastleBattleProtocolTest, SwitchHolderKeepsTheFirst1bBytes)
{
    const auto holder = std::get<Battle::SwitchHolder>(Read(SwitchHolderPacket(0x01, 0x3d)));
    EXPECT_EQ(holder.key, 0x013d);
    const std::vector<std::byte> packet = SwitchHolderPacket(0x01, 0x3d);
    for (std::size_t i = 0; i < Battle::SwitchHolderSize; ++i)
    {
        EXPECT_EQ(holder.bytes.at(i), std::to_integer<std::uint8_t>(packet.at(i))) << i;
    }
}

TEST(CastleBattleProtocolTest, SwitchHolderWithout1bBytesIsTooShort)
{
    std::vector<std::byte> packet = SwitchHolderPacket(0x01, 0x3e);
    packet.resize(0x1a);
    packet[1] = std::byte{0x1a};
    EXPECT_EQ(ErrorReading(std::move(packet)), ProtocolError::TooShort);
}

// Los otros B2 son del hito 12 (Protocol/CastleSiege.h) o no tienen handler: acá son WrongPacket, como otro opcode.
TEST(CastleBattleProtocolTest, OtherOpcodesAndB2SubcodesAreWrongPackets)
{
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 5, 0xb2, 0x1f, 0x01})), ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 5, 0xb3, 0x20, 0x01})), ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 3, 0xb9})), ProtocolError::TooShort);
}
} // namespace
