#include "Protocol/Duel.h"
#include "Protocol/Party.h"
#include "Protocol/ProtocolError.h"
#include "Protocol/Trade.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/Packets.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// 04 § El comercio, § La party y § El duelo (CL9 hito 7): los lectores y los escritores, byte a byte. Los nombres son
// inventados.
namespace
{
using Client::Network::Delivery;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::ProtocolError;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;
using Client::Tests::SameBytes;
namespace Trade = Client::Protocol::Trade;
namespace Party = Client::Protocol::Party;
namespace Duel = Client::Protocol::Duel;

// "Arbusto" y "Brezal", nombres inventados, con ceros hasta los 10 bytes.
constexpr std::array<std::byte, 10> Arbusto{std::byte{'A'}, std::byte{'r'}, std::byte{'b'}, std::byte{'u'},
                                            std::byte{'s'}, std::byte{'t'}, std::byte{'o'}};
constexpr std::array<std::byte, 10> Brezal{std::byte{'B'}, std::byte{'r'}, std::byte{'e'},
                                           std::byte{'z'}, std::byte{'a'}, std::byte{'l'}};

// Una vuelta entera de comercio en el cable, en el orden en que pasa: el pedido del otro (36), el sí (37 01), la
// respuesta del GS (37), un ítem del otro (39), el zen propio (3A), el aceptado del GS (3A), el zen del otro (3B), el
// OK propio (3C 01), el OK del otro (3C 01) y el resultado (3D 01).
TEST(TradeProtocolTest, AWholeRoundByteByByte)
{
    const auto request =
        Trade::ReadTradeRequest(PacketOf(Bytes({0xc1, 0x0d, 0x36, 'A', 'r', 'b', 'u', 's', 't', 'o', 0, 0, 0})));
    ASSERT_TRUE(request.HasValue());
    EXPECT_EQ(request.Value().name, Arbusto);

    const auto yes = Trade::WriteTradeResponse(1);
    EXPECT_EQ(yes.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(Xor32Decode(yes.packet).Bytes(), Bytes({0xc1, 0x04, 0x37, 0x01})));

    const auto response = Trade::ReadTradeResponse(PacketOf(Bytes(
        {0xc1, 0x14, 0x37, 0x01, 'A', 'r', 'b', 'u', 's', 't', 'o', 0, 0, 0, 0x90, 0x01, 0x78, 0x56, 0x34, 0x12})));
    ASSERT_TRUE(response.HasValue());
    EXPECT_EQ(response.Value().response, 1);
    EXPECT_EQ(response.Value().name, Arbusto);
    EXPECT_EQ(response.Value().level, 400);
    EXPECT_EQ(response.Value().guild, 0x12345678u);

    const auto added =
        Trade::ReadTradeItemAdded(PacketOf(Bytes({0xc1, 0x0b, 0x39, 0x09, 0x21, 0x0b, 0x2c, 0x00, 0x00, 0x70, 0x00})));
    ASSERT_TRUE(added.HasValue());
    EXPECT_EQ(added.Value().slot, 9);
    EXPECT_EQ(added.Value().item[0], std::byte{0x21});
    EXPECT_EQ(added.Value().item[5], std::byte{0x70});

    const auto money = Trade::WriteTradeMoney(0x0001e240);
    EXPECT_EQ(money.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(Xor32Decode(money.packet).Bytes(), Bytes({0xc1, 0x08, 0x3a, 0x00, 0x40, 0xe2, 0x01, 0x00})));

    const auto accepted = Trade::ReadTradeMoneyResult(PacketOf(Bytes({0xc1, 0x04, 0x3a, 0x01})));
    ASSERT_TRUE(accepted.HasValue());
    EXPECT_EQ(accepted.Value().accepted, 1);

    const auto other = Trade::ReadTradeOtherMoney(PacketOf(Bytes({0xc1, 0x08, 0x3b, 0x00, 0x10, 0x27, 0x00, 0x00})));
    ASSERT_TRUE(other.HasValue());
    EXPECT_EQ(other.Value().money, 10000u);

    const auto ok = Trade::WriteTradeOkButton(1);
    EXPECT_EQ(ok.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Xor32Decode(ok.packet).Bytes(), Bytes({0xc1, 0x04, 0x3c, 0x01})));

    const auto otherOk = Trade::ReadTradeOkState(PacketOf(Bytes({0xc1, 0x04, 0x3c, 0x01})));
    ASSERT_TRUE(otherOk.HasValue());
    EXPECT_EQ(otherOk.Value().state, 1);

    const auto result = Trade::ReadTradeResult(PacketOf(Bytes({0xc1, 0x04, 0x3d, 0x01})));
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value().result, 1);
    EXPECT_EQ(result.Value().head, (std::array<std::uint8_t, 4>{0xc1, 0x04, 0x3d, 0x01}));
}

TEST(TradeProtocolTest, TheOtherWriters)
{
    const auto request = Trade::WriteTradeRequest(0x1234);
    EXPECT_EQ(request.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Xor32Decode(request.packet).Bytes(), Bytes({0xc1, 0x05, 0x36, 0x12, 0x34})));
    const auto no = Trade::WriteTradeResponse(0);
    EXPECT_TRUE(SameBytes(Xor32Decode(no.packet).Bytes(), Bytes({0xc1, 0x04, 0x37, 0x00})));
    const auto cancel = Trade::WriteTradeCancel();
    EXPECT_EQ(cancel.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Xor32Decode(cancel.packet).Bytes(), Bytes({0xc1, 0x03, 0x3d})));
    const auto report = Trade::WriteClearTradeRequestReport();
    EXPECT_EQ(report.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Xor32Decode(report.packet).Bytes(), Bytes({0xc1, 0x06, 0xf1, 0x03, 0x00, 0x36})));
}

TEST(TradeProtocolTest, ShortOrWrongPackets)
{
    EXPECT_EQ(ErrorOf(Trade::ReadTradeRequest(PacketOf(Bytes({0xc1, 0x05, 0x36, 'A', 'r'})))), ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(Trade::ReadTradeResponse(PacketOf(Bytes({0xc1, 0x04, 0x37, 0x01})))), ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(Trade::ReadTradeItemAdded(PacketOf(Bytes({0xc1, 0x06, 0x39, 0x01, 0x21, 0x0b})))),
              ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(Trade::ReadTradeItemRemoved(PacketOf(Bytes({0xc1, 0x04, 0x39, 0x01})))),
              ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(Trade::ReadTradeResult(PacketOf(Bytes({0xc1, 0x03, 0x3d})))), ProtocolError::TooShort);
}

// 0x4d67e0: dos miembros de 0x18 bytes desde pkt[5]; los bytes +0xe y +0xf no se leen.
TEST(PartyProtocolTest, ReadsTheListOfTwoMembers)
{
    const auto list = Party::ReadPartyList(PacketOf(
        Bytes({0xc1, 0x35, 0x42, 0x01, 0x02,
               // Arbusto: el número 0, Lorencia (0), la celda (130, 120), dos bytes de relleno, 500 de 800.
               'A', 'r', 'b', 'u', 's', 't', 'o', 0, 0, 0, 0x00, 0x00, 0x82, 0x78, 0xee, 0xee, 0xf4, 0x01, 0, 0, 0x20,
               0x03, 0, 0,
               // Brezal: el número 3, Devias (2), la celda (200, 30), 75 de 100.
               'B', 'r', 'e', 'z', 'a', 'l', 0, 0, 0, 0, 0x03, 0x02, 0xc8, 0x1e, 0, 0, 0x4b, 0, 0, 0, 0x64, 0, 0, 0})));
    ASSERT_TRUE(list.HasValue());
    ASSERT_EQ(list.Value().members.size(), 2u);
    const Party::PartyMember& first = list.Value().members[0];
    EXPECT_EQ(first.name, Arbusto);
    EXPECT_EQ(first.number, 0);
    EXPECT_EQ(first.x, 0x82);
    EXPECT_EQ(first.y, 0x78);
    EXPECT_EQ(first.life, 500u);
    EXPECT_EQ(first.maxLife, 800u);
    const Party::PartyMember& second = list.Value().members[1];
    EXPECT_EQ(second.name, Brezal);
    EXPECT_EQ(second.number, 3);
    EXPECT_EQ(second.map, 2);
    EXPECT_EQ(second.life, 75u);
    EXPECT_EQ(second.maxLife, 100u);
    // La cuenta pide más miembros de los que trae.
    EXPECT_EQ(ErrorOf(Party::ReadPartyList(PacketOf(Bytes({0xc1, 0x06, 0x42, 0x01, 0x01, 'A'})))),
              ProtocolError::TooShort);
}

TEST(PartyProtocolTest, TheRequestTheResultTheLeaveAndTheLife)
{
    const auto request = Party::ReadPartyRequest(PacketOf(Bytes({0xc1, 0x05, 0x40, 0x12, 0x34})));
    ASSERT_TRUE(request.HasValue());
    EXPECT_EQ(request.Value().key, 0x1234);
    const auto result = Party::ReadPartyRequestResult(PacketOf(Bytes({0xc1, 0x04, 0x41, 0x05})));
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value().result, 5);
    EXPECT_TRUE(Party::ReadPartyLeft(PacketOf(Bytes({0xc1, 0x03, 0x43}))).HasValue());
    const auto life = Party::ReadPartyLife(PacketOf(Bytes({0xc1, 0x06, 0x44, 0x02, 0x07, 0x1a})));
    ASSERT_TRUE(life.HasValue());
    EXPECT_EQ(life.Value().entries, (std::vector<std::uint8_t>{0x07, 0x1a}));
    EXPECT_EQ(ErrorOf(Party::ReadPartyLife(PacketOf(Bytes({0xc1, 0x05, 0x44, 0x02, 0x07})))), ProtocolError::TooShort);

    const auto invite = Party::WritePartyRequest(0x0102);
    EXPECT_EQ(invite.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Xor32Decode(invite.packet).Bytes(), Bytes({0xc1, 0x05, 0x40, 0x01, 0x02})));
    const auto answer = Party::WritePartyRequestResult(1, 0x1234);
    EXPECT_EQ(answer.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Xor32Decode(answer.packet).Bytes(), Bytes({0xc1, 0x06, 0x41, 0x01, 0x12, 0x34})));
    const auto leave = Party::WritePartyLeave(3);
    EXPECT_EQ(leave.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(Xor32Decode(leave.packet).Bytes(), Bytes({0xc1, 0x04, 0x43, 0x03})));
}

TEST(DuelProtocolTest, ReadsTheFourPackets)
{
    const auto start = Duel::ReadDuelStart(
        PacketOf(Bytes({0xc1, 0x10, 0xaa, 0x01, 0x00, 0x2a, 'B', 'r', 'e', 'z', 'a', 'l', 0, 0, 0, 0})));
    ASSERT_TRUE(start.HasValue());
    EXPECT_EQ(start.Value().result, 1);
    EXPECT_EQ(start.Value().key, 0x2a);
    EXPECT_EQ(start.Value().name, Brezal);
    const auto end =
        Duel::ReadDuelEnd(PacketOf(Bytes({0xc1, 0x0f, 0xab, 0x00, 0x2a, 'B', 'r', 'e', 'z', 'a', 'l', 0, 0, 0, 0})));
    ASSERT_TRUE(end.HasValue());
    EXPECT_EQ(end.Value().key, 0x2a);
    const auto request = Duel::ReadDuelRequest(
        PacketOf(Bytes({0xc1, 0x0f, 0xac, 0x01, 0x02, 'B', 'r', 'e', 'z', 'a', 'l', 0, 0, 0, 0})));
    ASSERT_TRUE(request.HasValue());
    EXPECT_EQ(request.Value().key, 0x0102);
    EXPECT_EQ(request.Value().name, Brezal);
    const auto score = Duel::ReadDuelScore(PacketOf(Bytes({0xc1, 0x09, 0xad, 0x00, 0x05, 0x00, 0x2a, 3, 1})));
    ASSERT_TRUE(score.HasValue());
    EXPECT_EQ(score.Value().firstKey, 5);
    EXPECT_EQ(score.Value().secondKey, 0x2a);
    EXPECT_EQ(score.Value().firstScore, 3);
    EXPECT_EQ(score.Value().secondScore, 1);
    EXPECT_EQ(ErrorOf(Duel::ReadDuelScore(PacketOf(Bytes({0xc1, 0x08, 0xad, 0, 5, 0, 0x2a, 3})))),
              ProtocolError::TooShort);
}

TEST(DuelProtocolTest, TheThreeWriters)
{
    const auto start = Duel::WriteDuelStart(0x002a, Brezal);
    EXPECT_EQ(start.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Xor32Decode(start.packet).Bytes(),
                          Bytes({0xc1, 0x0f, 0xaa, 0x00, 0x2a, 'B', 'r', 'e', 'z', 'a', 'l', 0, 0, 0, 0})));
    const auto end = Duel::WriteDuelEnd();
    EXPECT_EQ(end.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Xor32Decode(end.packet).Bytes(), Bytes({0xc1, 0x03, 0xab})));
    const auto answer = Duel::WriteDuelAnswer(0, 0x0102, Arbusto);
    EXPECT_EQ(answer.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Xor32Decode(answer.packet).Bytes(),
                          Bytes({0xc1, 0x10, 0xac, 0x00, 0x01, 0x02, 'A', 'r', 'b', 'u', 's', 't', 'o', 0, 0, 0})));
}
} // namespace
