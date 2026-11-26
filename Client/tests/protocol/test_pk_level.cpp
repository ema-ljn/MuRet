#include "Protocol/Player/PkLevel.h"

#include "Network/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/Packets.h"

#include <gtest/gtest.h>

// El estado de PK de un personaje (F3 08, S->C), 0x4d3b50: la clave en pkt[4..5], el byte alto primero, y el estado en
// pkt[6].
namespace
{
using Client::Network::ProtocolError;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;
namespace Player = Client::Protocol::Player;

TEST(PkLevelProtocolTest, ReadsTheKeyBigEndianAndTheLevel)
{
    const auto read = Player::ReadPkLevel(PacketOf(Bytes({0xc1, 0x07, 0xf3, 0x08, 0x12, 0x34, 0x05})));
    ASSERT_TRUE(read.HasValue());
    EXPECT_EQ(read.Value().key, 0x1234);
    EXPECT_EQ(read.Value().level, 5);
}

// 0x4d3b5f..0x4d3b86 lee pkt[4], pkt[5] y pkt[6]: sin los siete bytes, TooShort; otro subcódigo, WrongPacket.
TEST(PkLevelProtocolTest, NeedsSevenBytesAndTheSubcode08)
{
    EXPECT_EQ(ErrorOf(Player::ReadPkLevel(PacketOf(Bytes({0xc1, 0x06, 0xf3, 0x08, 0x12, 0x34})))),
              ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(Player::ReadPkLevel(PacketOf(Bytes({0xc1, 0x07, 0xf3, 0x06, 0x12, 0x34, 0x05})))),
              ProtocolError::WrongPacket);
}
} // namespace
