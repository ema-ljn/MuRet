#include "Network/PacketSerials.h"

#include "support/NetworkPrinters.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace
{
using Client::Network::PacketSerials;
using Client::Network::SerialCheck;

constexpr int SerialCount = 256;

PacketSerials ExpectingReceive(std::uint8_t expected)
{
    PacketSerials serials;
    for (int serial = 0; serial < expected; ++serial)
    {
        EXPECT_EQ(serials.Receive(static_cast<std::uint8_t>(serial)), SerialCheck::InOrder);
    }
    return serials;
}

TEST(PacketSerialsTest, SendCountsFromZeroAndWraps)
{
    PacketSerials serials;

    for (int serial = 0; serial < SerialCount; ++serial)
    {
        ASSERT_EQ(serials.TakeSend(), serial);
    }
    EXPECT_EQ(serials.TakeSend(), 0);
}

// Q-C04: expecting 5, 5, 9 and 10 arrive. The original, with 9, stays at 9 without adding 1, and 10 does not match
// either: two "Decrypt error", and the offset stays (red_reference.py's case 5). Here 9 resyncs and 10 is in order.
TEST(PacketSerialsTest, ResyncsWithTheOneThatCame)
{
    PacketSerials serials = ExpectingReceive(5);

    EXPECT_EQ(serials.Receive(5), SerialCheck::InOrder);
    EXPECT_EQ(serials.Receive(9), SerialCheck::Resynced);
    EXPECT_EQ(serials.Receive(10), SerialCheck::InOrder);
    EXPECT_EQ(serials.Receive(11), SerialCheck::InOrder);
}

TEST(PacketSerialsTest, OneLostPacketResyncs)
{
    PacketSerials serials = ExpectingReceive(5);

    EXPECT_EQ(serials.Receive(6), SerialCheck::Resynced);
    EXPECT_EQ(serials.Receive(7), SerialCheck::InOrder);
}

// After 3, 4 is expected, not 7.
TEST(PacketSerialsTest, ARepeatedSerialResyncsBackwards)
{
    PacketSerials serials = ExpectingReceive(7);

    EXPECT_EQ(serials.Receive(3), SerialCheck::Resynced);
    EXPECT_EQ(serials.Receive(4), SerialCheck::InOrder);
}

TEST(PacketSerialsTest, ReceiveWrapsAfter255)
{
    PacketSerials serials = ExpectingReceive(255);

    EXPECT_EQ(serials.Receive(255), SerialCheck::InOrder);
    EXPECT_EQ(serials.Receive(0), SerialCheck::InOrder);
}

// A packet that could not be decrypted spends the expected serial, 255 too.
TEST(PacketSerialsTest, SkipReceiveExpectsTheNextOne)
{
    PacketSerials serials;
    serials.SkipReceive();
    EXPECT_EQ(serials.Receive(1), SerialCheck::InOrder);

    PacketSerials atTheEnd = ExpectingReceive(255);
    atTheEnd.SkipReceive();
    EXPECT_EQ(atTheEnd.Receive(0), SerialCheck::InOrder);
}

TEST(PacketSerialsTest, SendAndReceiveAreIndependent)
{
    PacketSerials serials;

    EXPECT_EQ(serials.TakeSend(), 0);
    EXPECT_EQ(serials.TakeSend(), 1);
    EXPECT_EQ(serials.Receive(0), SerialCheck::InOrder);
    serials.SkipReceive();
    EXPECT_EQ(serials.TakeSend(), 2);
    EXPECT_EQ(serials.Receive(2), SerialCheck::InOrder);
}

// Like 0x4c1340 when connecting to the GameServer.
TEST(PacketSerialsTest, ResetStartsBothOver)
{
    constexpr std::uint8_t ReceivedBefore = 40;
    constexpr int SentBefore = 9;
    PacketSerials serials = ExpectingReceive(ReceivedBefore);
    for (int k = 0; k < SentBefore; ++k)
    {
        [[maybe_unused]] const std::uint8_t sent = serials.TakeSend();
    }

    serials.Reset();

    EXPECT_EQ(serials.TakeSend(), 0);
    EXPECT_EQ(serials.Receive(0), SerialCheck::InOrder);
}
} // namespace
