#include "Protocol/Quest.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/Packets.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

// 04 § Las misiones y los NPC: los 9F y A0..A3 que llegan y el A0, el A2 y el 30 que salen.
namespace
{
using Client::Network::Delivery;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::ProtocolError;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;
using Client::Tests::SameBytes;
namespace Quest = Client::Protocol::Quest;

// 0x63693a y 0x5676a6: C1 03 A0, con el XOR32 (nada que cifrar después de pkt[2]) y entrega C3.
TEST(QuestProtocolTest, TheListRequestIsAnEncryptedC1OfThreeBytes)
{
    const auto request = Quest::WriteQuestListRequest();
    EXPECT_EQ(request.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Xor32Decode(request.packet).Bytes(), Bytes({0xc1, 0x03, 0xa0})));
}

// 0x419d13: C1 05 A2 misión estado.
TEST(QuestProtocolTest, TheSetQuestIsC105A2WithTheQuestAndTheState)
{
    const auto request = Quest::WriteSetQuest(2, 1);
    EXPECT_EQ(request.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Xor32Decode(request.packet).Bytes(), Bytes({0xc1, 0x05, 0xa2, 0x02, 0x01})));
    // Lo que sale lleva el XOR32 desde pkt[3] con la clave de 0x419bd0 (e7 6d 3a 89 bc ...): cada byte con el de su
    // lugar y el anterior ya cifrado.
    constexpr int Third = 0x02 ^ 0x89 ^ 0xa2;
    EXPECT_TRUE(SameBytes(request.packet.Bytes(), Bytes({0xc1, 0x05, 0xa2, Third, 0x01 ^ 0xbc ^ Third})));
}

// 0x567cca..0x567cd4: C1 05 30 y la clave del NPC, el byte alto con SAR.
TEST(QuestProtocolTest, TheTalkCarriesTheKeyHighByteFirst)
{
    const auto talk = Quest::WriteTalk(0x0123);
    EXPECT_EQ(talk.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Xor32Decode(talk.packet).Bytes(), Bytes({0xc1, 0x05, 0x30, 0x01, 0x23})));
    const auto negative = Quest::WriteTalk(static_cast<std::int16_t>(-2));
    EXPECT_TRUE(SameBytes(Xor32Decode(negative.packet).Bytes(), Bytes({0xc1, 0x05, 0x30, 0xff, 0xfe})));
}

TEST(QuestProtocolTest, ReadsTheFlag)
{
    const auto flag = Quest::ReadQuestFlag(PacketOf(Bytes({0xc1, 0x05, 0x9f, 0x02, 0x41})));
    ASSERT_TRUE(flag.HasValue());
    EXPECT_EQ(flag.Value().index, 2);
    EXPECT_EQ(flag.Value().value, 0x41);
    EXPECT_EQ(ErrorOf(Quest::ReadQuestFlag(PacketOf(Bytes({0xc1, 0x04, 0x9f, 0x02})))), ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(Quest::ReadQuestFlag(PacketOf(Bytes({0xc1, 0x05, 0xa0, 0x02, 0x41})))),
              ProtocolError::WrongPacket);
}

// Q-C840: el GS 0.97 manda la cuenta sin los bytes (C1 04 A0 cuenta); el que los trae los deja todos.
TEST(QuestProtocolTest, ReadsTheListWithOrWithoutItsStates)
{
    const auto bare = Quest::ReadQuestList(PacketOf(Bytes({0xc1, 0x04, 0xa0, 0x03})));
    ASSERT_TRUE(bare.HasValue());
    EXPECT_EQ(bare.Value().count, 3);
    EXPECT_TRUE(bare.Value().states.empty());
    const auto full = Quest::ReadQuestList(PacketOf(Bytes({0xc1, 0x07, 0xa0, 0x02, 0xaa, 0x01, 0x09})));
    ASSERT_TRUE(full.HasValue());
    EXPECT_EQ(full.Value().count, 2);
    EXPECT_EQ(full.Value().states, (std::vector<std::uint8_t>{0xaa, 0x01, 0x09}));
    EXPECT_EQ(ErrorOf(Quest::ReadQuestList(PacketOf(Bytes({0xc1, 0x03, 0xa0})))), ProtocolError::TooShort);
}

TEST(QuestProtocolTest, ReadsTheStateAndTheSetQuestResult)
{
    const auto state = Quest::ReadQuestState(PacketOf(Bytes({0xc1, 0x05, 0xa1, 0x01, 0x46})));
    ASSERT_TRUE(state.HasValue());
    EXPECT_EQ(state.Value().index, 1);
    EXPECT_EQ(state.Value().state, 0x46);
    const auto result = Quest::ReadSetQuestResult(PacketOf(Bytes({0xc1, 0x06, 0xa2, 0x01, 0x00, 0x46})));
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value().index, 1);
    EXPECT_EQ(result.Value().result, 0);
    EXPECT_EQ(result.Value().state, 0x46);
    EXPECT_EQ(ErrorOf(Quest::ReadSetQuestResult(PacketOf(Bytes({0xc1, 0x05, 0xa2, 0x01, 0x00})))),
              ProtocolError::TooShort);
}

// 0x4ddb8c..0x4ddba0: (pkt[3] · 0x100 + pkt[4]) & 0x7fff, el tipo y el valor.
TEST(QuestProtocolTest, ReadsTheResultWithTheKeyWithoutItsTopBit)
{
    const auto result = Quest::ReadQuestResult(PacketOf(Bytes({0xc1, 0x07, 0xa3, 0x81, 0x02, 0xc9, 0x10})));
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value().key, 0x0102);
    EXPECT_EQ(result.Value().kind, Quest::ClassChangePrize);
    EXPECT_EQ(result.Value().value, 0x10);
    EXPECT_EQ(ErrorOf(Quest::ReadQuestResult(PacketOf(Bytes({0xc1, 0x06, 0xa3, 0x81, 0x02, 0xc9})))),
              ProtocolError::TooShort);
}
} // namespace
